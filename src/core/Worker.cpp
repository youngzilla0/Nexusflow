#include "Worker.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "utils/logging.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

namespace nexusflow { namespace core {

Worker::Worker(const std::shared_ptr<Module>& modulePtr,
               const PipelineConfig& runtimeConfig) {
    m_modulePtr = modulePtr;
    m_runtimeConfig = runtimeConfig;
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
    return ErrorCode::SUCCESS;
}

ErrorCode Worker::Stop() {
    // Check if the worker is already stopped.
    if (!m_stopFlag.load()) {
        LOG_TRACE("Stopping worker for module: {}", m_modulePtr->GetModuleName());
        m_stopFlag.store(true);
        return ErrorCode::SUCCESS;
    } else {
        LOG_WARN("Worker is already stopped.");
        return ErrorCode::FAILURE;
    }
}

void Worker::WorkLoop() {
    LOG_DEBUG("Worker for module '{}' started", m_modulePtr->GetModuleName());

    bool isSourceModule = m_inputQueueMap.empty(); // Check if this is a source module.

    // Read batch parameters from runtimeConfig
    size_t maxBatchSize = m_runtimeConfig.maxBatchSize;
    auto batchTimeout = std::chrono::milliseconds(m_runtimeConfig.batchTimeoutMs);

    bool isJoinInputs = m_modulePtr->JoinInputs();

    LOG_DEBUG("Worker for module '{}' is running. Is source module: {}. Is join inputs: {}. Batch size: {}, Timeout: {}ms.",
              m_modulePtr->GetModuleName(), isSourceModule, isJoinInputs, maxBatchSize, m_runtimeConfig.batchTimeoutMs);

    if (isJoinInputs) {
        assert(!isSourceModule);
        RunFusion(); // Run the fusion module.
    } else {
        while (!m_stopFlag.load()) {
            if (isSourceModule) {
                // Source Module Loop
                Message emptyMessage;
                m_modulePtr->Process(emptyMessage);
            } else {
                // Sink or Filter/Transformer Module Loop
                auto batchMessage = PullBatchMessage(maxBatchSize, batchTimeout);
                m_modulePtr->ProcessBatch(batchMessage);
            }
        }
    }

    LOG_DEBUG("Worker for module '{}' finished.", m_modulePtr->GetModuleName());
}

void Worker::RunFusion() {
    // Key: message id (uint64_t, matches Message::messageId nanoseconds timestamp).
    // Value: map of input queue name -> message (using queue name not sourceName
    // because sourceName is "" for both Pass1 and Pass2 in Diamond pipeline).
    std::unordered_map<uint64_t, std::unordered_map<std::string, Message>> messageCache;
    const int expectedInputCount = m_inputQueueMap.size();
    uint64_t iterCount = 0;

    // Define a timeout period.
    constexpr std::chrono::minutes timeout{1};
    uint64_t timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();

    while (!m_stopFlag.load()) {
        iterCount++;

        uint64_t currentTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

        // collect message from all inputs
        for (auto& queuePair : m_inputQueueMap) {
            auto& queue = queuePair.second;
            Message message;
            if (queue->tryPop(message)) {
                auto messageId = message.GetMetaData().messageId;
                // Use queue name (e.g. "Pass1 -> Sink") as key to distinguish inputs
                messageCache[messageId][queuePair.first] = message;
            }
        }

        // check message cache for entries ready to process
        for (auto it = messageCache.begin(); it != messageCache.end();) {
            auto messageId = it->first;
            auto& messageMap = it->second;

            if (messageMap.size() == expectedInputCount) {
                // All inputs received for this message id -> fuse and process
                auto fusedMessage = MakeMessage(std::move(messageMap));
                std::vector<Message> fusedMessageVec{fusedMessage};
                m_modulePtr->ProcessBatch(fusedMessageVec);
                it = messageCache.erase(it);
            } else if (!messageMap.empty() && messageMap.begin()->second.GetMetaData().timestamp < (currentTimeMs - timeoutMs)) {
                // timeout: remove stale entry
                it = messageCache.erase(it);
            } else {
                ++it;
            }
        }

        // Adaptive sleep: avoid busy-spinning when cache is empty (no messages waiting)
        if (messageCache.empty()) {
            std::this_thread::sleep_for(std::chrono::microseconds(5));
        }
    }
}

std::vector<Message> Worker::PullBatchMessage(size_t maxBatchSize, std::chrono::milliseconds batchTimeout) {
    // Initialize the batch vector.
    std::vector<Message> batchMessage;

    // Ensure the output vector is clean and has pre-allocated memory.
    batchMessage.clear();
    batchMessage.reserve(maxBatchSize);

    auto startTime = std::chrono::steady_clock::now();

    // --- Phase 1: Greedy non-blocking pull ---
    // Quickly drain any messages that are already waiting in the queues.
    for (auto& item : m_inputQueueMap) {
        auto& queue = item.second;
        while (batchMessage.size() < maxBatchSize) {
            Message message;
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

    // --- Phase 2: Non-blocking polling loop (eliminates syscall overhead) ---
    // Instead of using blocking waitAndPopFor which causes mutex contention,
    // use a tight non-blocking poll with sleep when queue is empty.
    // This reduces context switches and lock contention significantly.
    while (!m_stopFlag.load() && batchMessage.size() < maxBatchSize) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
        if (elapsed >= batchTimeout) {
            break;
        }

        bool foundMessage = false;
        for (auto& item : m_inputQueueMap) {
            auto& queue = item.second;
            Message msg;
            // Non-blocking pop - avoids mutex contention from waitAndPopFor
            if (queue->tryPop(msg)) {
                batchMessage.push_back(std::move(msg));
                foundMessage = true;
                // Drain this queue quickly
                while (batchMessage.size() < maxBatchSize) {
                    Message message;
                    if (queue->tryPop(message)) {
                        batchMessage.push_back(std::move(message));
                    } else {
                        break;
                    }
                }
            }
            if (batchMessage.size() >= maxBatchSize) break;
        }

        // If no messages found, sleep briefly to avoid busy-spinning
        // Use a small sleep to reduce CPU usage when queue is empty
        if (!foundMessage && batchMessage.size() < maxBatchSize) {
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    return batchMessage;
}

}} // namespace nexusflow::core