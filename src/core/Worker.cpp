#include "Worker.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "utils/logging.hpp"
#include <memory>

namespace nexusflow { namespace core {

Worker::Worker(const std::shared_ptr<Module>& modulePtr) {
    m_modulePtr = modulePtr; // Store the module pointer.
    m_stopFlag = false;
}

Worker::~Worker() {
    // Signal the worker to stop.
    LOG_TRACE("Destroying worker for module: {}", m_modulePtr->GetModuleName());
    if (!m_stopFlag) {
        LOG_WARN("Worker is still running, stopping it now.");
        Stop();
    }
}

ErrorCode Worker::Start() {
    // Check if the worker is already running.
    if (m_stopFlag.load()) {
        LOG_WARN("Worker is already running.");
        return ErrorCode::FAILURE;
    }

    // Start the worker thread.
    m_stopFlag.store(false);
    m_thread = std::thread(&Worker::Run, this);
    return ErrorCode::SUCCESS;
}

ErrorCode Worker::Stop() {
    // Check if the worker is already stopped.
    if (!m_stopFlag.load()) {
        LOG_TRACE("Stopping worker for module: {}", m_modulePtr->GetModuleName());
        m_stopFlag.store(true);
        if (m_thread.joinable()) m_thread.join();
        return ErrorCode::SUCCESS;
    } else {
        LOG_WARN("Worker is already stopped.");
        return ErrorCode::FAILURE;
    }
}

void Worker::Run() {
    LOG_DEBUG("Worker for module '{}' started", m_modulePtr->GetModuleName());

    bool isSourceModule = m_inputQueueMap.empty(); // Check if this is a source module.

    /**
     * TODO: yzl
     * 1. Add try-catch block to handle exceptions.
     * 2. Configure variables for batch size and timeout from config.
     */
    constexpr size_t kMaxBatchSize = 4;
    constexpr std::chrono::milliseconds kBatchTimeout{100};

    while (!m_stopFlag.load()) {
        if (isSourceModule) {
            // Source Module Loop
            m_modulePtr->Process(nullptr);
        } else {
            // Sink or Filter/Transformer Module Loop
            auto batchMessage = PullBatchMessage(kMaxBatchSize, kBatchTimeout);
            m_modulePtr->ProcessBatch(batchMessage);
        }
    }

    LOG_DEBUG("Worker for module '{}' finished.", m_modulePtr->GetModuleName());
}

std::vector<std::shared_ptr<Message>> Worker::PullBatchMessage(size_t maxBatchSize, std::chrono::milliseconds batchTimeout) {
    // Initialize the batch vector.
    std::vector<std::shared_ptr<Message>> batchMessage;

    // Ensure the output vector is clean and has pre-allocated memory.
    batchMessage.clear();
    batchMessage.reserve(maxBatchSize);

    auto startTime = std::chrono::steady_clock::now();

    // --- Phase 1: Greedy non-blocking pull ---
    // Quickly drain any messages that are already waiting in the queues.
    for (auto& item : m_inputQueueMap) {
        auto& queue = item.second;
        while (batchMessage.size() < maxBatchSize) {
            std::shared_ptr<Message> message;
            if (queue->tryPop(message)) {
                batchMessage.push_back(std::move(message));
            } else {
                // This queue is empty, so move on to the next one.
                break;
            }
        }
        if (batchMessage.size() >= maxBatchSize) {
            return batchMessage; // Batch is full, no need to wait.
        }
    }

    // --- Phase 2: Short-blocking polling loop ---
    // If the batch is not yet full, enter a polling loop that waits efficiently.
    while (!m_stopFlag.load()) {
        // Check exit condition: batch is full.
        if (batchMessage.size() >= maxBatchSize) {
            break;
        }

        // Check exit condition: total time has elapsed.
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
        if (elapsed >= batchTimeout) {
            break;
        }

        // Iterate through all input queues and perform a short wait on each.
        for (auto& item : m_inputQueueMap) {
            auto& queue = item.second;
            std::shared_ptr<Message> msg;
            // Wait for a very short period (e.g., 1ms). This is the key to avoiding
            // busy-waiting while remaining responsive to multiple inputs.
            if (queue->waitAndPopFor(msg, std::chrono::milliseconds(1))) {
                batchMessage.push_back(std::move(msg));
                // Optimization: If a message was found, this queue might have more.
                // Try to pop more in a non-blocking way to fill the batch faster.
                while (batchMessage.size() < maxBatchSize) {
                    std::shared_ptr<Message> message;
                    if (queue->tryPop(message)) {
                        batchMessage.push_back(std::move(message));
                    } else {
                        break; // The queue is now empty.
                    }
                }
            }
            // Check if the batch became full during the inner loop.
            if (batchMessage.size() >= maxBatchSize) {
                break;
            }
        }
    }

    return batchMessage;
}

}} // namespace nexusflow::core