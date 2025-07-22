#include "Worker.hpp"
#include "module/ActorContext.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/Module.hpp"
#include "nexusflow/ProcessingContext.hpp"
#include "utils/logging.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <unordered_map>

namespace nexusflow { namespace core {

Worker::Worker(const std::shared_ptr<Module>& modulePtr, const ActorContext& context) : m_context(context) {
    m_modulePtr = modulePtr;
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

    /**
     * TODO: yzl
     * 1. Add try-catch block to handle exceptions.
     * 2. Configure variables for batch size and timeout from config.
     */
    constexpr size_t kMaxBatchSize = 4;
    constexpr std::chrono::milliseconds kBatchTimeout{100};

    bool isSyncInputs = false;
    isSyncInputs = m_context.config->GetValueOrDefault<bool>("syncInputs", isSyncInputs);

    LOG_INFO("Worker for module '{}' is running. Is source module: {}. Is sync inputs: {}.", m_modulePtr->GetModuleName(),
             isSourceModule, isSyncInputs);

    if (isSyncInputs) {
        assert(!isSourceModule);
        RunFusion(); // Run the fusion module.
    } else {
        while (!m_stopFlag.load()) {
            std::vector<ProcessingContext> batchProcessingContext;

            if (isSourceModule) {
                // Source Module Loop
                batchProcessingContext.push_back(ProcessingContext(nexusflow::Message()));
                m_modulePtr->ProcessBatch(batchProcessingContext);
            } else {
                // Sink or Filter/Transformer Module Loop
                batchProcessingContext = PullBatchMessage(kMaxBatchSize, kBatchTimeout);
                m_modulePtr->ProcessBatch(batchProcessingContext);
            }

            // Get output messages from the module.
            for (auto& processingContext : batchProcessingContext) {
                auto outputMessages = processingContext.CollectOutputs();
                for (auto& message : outputMessages) {
                    // Push the message to the output queue.
                    m_dispatcher->Broadcast(message);
                }
            }
        }
    }

    LOG_DEBUG("Worker for module '{}' finished.", m_modulePtr->GetModuleName());
}

void Worker::RunFusion() {
    // TODO: 如何优化呢，现在只有单Batch, 并且需要测试下内存占用.

    // Key: message id, value: map of source module name and message.
    std::unordered_map<int, std::unordered_map<std::string, Message>> messageCache; // Cache for messages.
    const size_t expectedInputCount = m_inputQueueMap.size(); // Expected number of inputs.

    // Define a timeout period.
    constexpr std::chrono::minutes timeout{1};
    uint64_t timeoutMs = std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count();

    while (!m_stopFlag.load()) {
        uint64_t currentTimeMs =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

        // collect message from all inputs
        std::vector<Message> batchMessage;
        for (auto& queuePair : m_inputQueueMap) {
            auto& queue = queuePair.second;

            Message message;
            if (queue->tryPop(message)) {
                auto messageMeta = message.GetMetaData();
                auto messageId = messageMeta.messageId;
                auto& sourceModuleName = messageMeta.sourceName;
                messageCache[messageId][sourceModuleName] = message;
                LOG_DEBUG("Message with ID: {} received from source module: {}", messageId, sourceModuleName);
            }
        }

        // check message is already in cache
        for (auto it = messageCache.begin(); it != messageCache.end();) {
            auto messageId = it->first;
            auto& messageNamedMap = it->second;

            LOG_TRACE("Checking message with ID: {}", messageId);
            LOG_TRACE("Number of inputs received: {}, Expected inputs: {}, module name: {}", messageNamedMap.size(),
                      expectedInputCount, m_modulePtr->GetModuleName());
            if (messageNamedMap.size() == expectedInputCount) {
                // Construct a fused message and concat to batchMessage.
                std::vector<ProcessingContext> fusedProcessingContextVec{messageNamedMap};
                m_modulePtr->ProcessBatch(fusedProcessingContextVec); // process the fused message

                // Get output messages from the module.
                for (auto& processingContext : fusedProcessingContextVec) {
                    auto outputMessages = processingContext.CollectOutputs();
                    for (auto& message : outputMessages) {
                        // Push the message to the output queue.
                        m_dispatcher->Broadcast(message);
                    }
                }
                it = messageCache.erase(it); // remove the message from cache
            } else if (!messageNamedMap.empty() &&
                       messageNamedMap.begin()->second.GetMetaData().timestamp < (currentTimeMs - timeoutMs)) {
                // timeout
                LOG_WARN("Timeout for message with ID: {}, will be removed from cache", messageId);
                it = messageCache.erase(it);
            } else {
                ++it;
            }
        }

#if 0
        // Print the current state of the message cache.
        LOG_TRACE("Message cache state:");
        for (auto& pair : messageCache) {
            LOG_TRACE("Message ID: {}", pair.first);
            for (auto& innerPair : pair.second) {
                LOG_TRACE("Source module: {}, Timestamp: {}", innerPair.first, innerPair.second.GetMetaData().timstamp);
            }
        }
#endif
    }
}

std::vector<ProcessingContext> Worker::PullBatchMessage(size_t maxBatchSize, std::chrono::milliseconds batchTimeout) {
    // Initialize the batch vector.
    std::vector<ProcessingContext> batchProcessingContext;
    auto& batchMessage = batchProcessingContext;

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
            Message msg;
            // Wait for a very short period (e.g., 1ms). This is the key to avoiding
            // busy-waiting while remaining responsive to multiple inputs.
            if (queue->waitAndPopFor(msg, std::chrono::milliseconds(1))) {
                batchMessage.push_back(std::move(msg));
                // Optimization: If a message was found, this queue might have more.
                // Try to pop more in a non-blocking way to fill the batch faster.
                while (batchMessage.size() < maxBatchSize) {
                    Message message;
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

void Worker::Broadcast(const Message& message) {
    if (m_dispatcher != nullptr) {
        LOG_DEBUG("Module '{}' broadcasting message.", m_modulePtr->GetModuleName());
        m_dispatcher->Broadcast(message);
    } else {
        LOG_WARN("Module '{}' has no handle, cannot broadcast message.", m_modulePtr->GetModuleName());
    }
}

}} // namespace nexusflow::core