#include "ModuleBase.hpp" // Adjust path as necessary
#include "ErrorCode.hpp"
#include "helper/logging.hpp"

ModuleBase::ModuleBase(std::string name) : m_name(std::move(name)), m_stopFlag(false) {
    // Initialize the module with the given name.
    LOG_TRACE("Module '{}' created.", m_name);
}

ModuleBase::~ModuleBase() {
    LOG_TRACE("Module '{}' destroying...", m_name);
    // The Stop() call is essential to ensure the thread is joined before the object is fully destructed.
    Stop();
    LOG_TRACE("Module '{}' destroyed.", m_name);
}

ErrorCode ModuleBase::Init() {
    LOG_INFO("Initializing module '{}'...", m_name);
    return ErrorCode::SUCCESS;
}

ErrorCode ModuleBase::DeInit() {
    LOG_INFO("Deinitializing module '{}'...", m_name);
    return ErrorCode::SUCCESS;
}

void ModuleBase::Start() {
    if (!m_thread.joinable()) {
        LOG_INFO("Starting module '{}'...", m_name);
        // Ensure the stop flag is false before starting.
        m_stopFlag = false;
        m_thread = std::thread(&ModuleBase::Run, this);
    } else {
        LOG_WARN("Module '{}' already started.", m_name);
    }
}

void ModuleBase::Stop() {
    // Use exchange to ensure the "Stopping..." message is logged only once, even if Stop() is called multiple times.
    if (!m_stopFlag.exchange(true)) {
        LOG_INFO("Stopping module '{}'...", m_name);
        // Shut down all input queues to unblock the thread if it's waiting on a pop.
        for (auto const& item : m_inputQueueMap) {
            auto& queueName = item.first;
            auto& queue = item.second;
            LOG_DEBUG("Module '{}': Shutting down input port '{}'.", m_name, queueName);
            queue->shutdown();
        }
    }

    if (m_thread.joinable()) {
        m_thread.join();
        LOG_INFO("Module '{}' thread joined successfully.", m_name);
    }
}

void ModuleBase::addInputPort(const std::string& portName, const std::shared_ptr<MessageQueue>& queue) {
    LOG_DEBUG("Module '{}': Adding input port '{}'.", m_name, portName);
    m_inputQueueMap[portName] = queue;
}

void ModuleBase::addOutputPort(const std::string& portName, const std::shared_ptr<MessageQueue>& queue) {
    LOG_DEBUG("Module '{}': Adding output port '{}'.", m_name, portName);
    m_outputQueueMap[portName] = queue;
}

void ModuleBase::collectBatch(std::vector<std::shared_ptr<Message>>& batch, size_t maxBatchSize,
                              std::chrono::milliseconds totalTimeout) {
    // Ensure the output vector is clean and has pre-allocated memory.
    batch.clear();
    batch.reserve(maxBatchSize);

    auto startTime = std::chrono::steady_clock::now();

    // --- Phase 1: Greedy non-blocking pull ---
    // Quickly drain any messages that are already waiting in the queues.
    for (auto& item : m_inputQueueMap) {
        auto& queue = item.second;
        while (batch.size() < maxBatchSize) {
            if (auto msgOpt = queue->tryPop()) {
                batch.push_back(std::move(msgOpt.value()));
            } else {
                // This queue is empty, so move on to the next one.
                break;
            }
        }
        if (batch.size() >= maxBatchSize) {
            return; // Batch is full, no need to wait.
        }
    }

    // --- Phase 2: Short-blocking polling loop ---
    // If the batch is not yet full, enter a polling loop that waits efficiently.
    while (!m_stopFlag.load()) {
        // Check exit condition: batch is full.
        if (batch.size() >= maxBatchSize) {
            break;
        }

        // Check exit condition: total time has elapsed.
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - startTime);
        if (elapsed >= totalTimeout) {
            break;
        }

        // Iterate through all input queues and perform a short wait on each.
        for (auto& item : m_inputQueueMap) {
            std::shared_ptr<Message> msg;

            // Wait for a very short period (e.g., 1ms). This is the key to avoiding
            // busy-waiting while remaining responsive to multiple inputs.
            if (item.second->waitAndPopFor(msg, std::chrono::milliseconds(1))) {
                batch.push_back(std::move(msg));

                // Optimization: If a message was found, this queue might have more.
                // Try to pop more in a non-blocking way to fill the batch faster.
                while (batch.size() < maxBatchSize) {
                    if (auto msgOpt = item.second->tryPop()) {
                        batch.push_back(std::move(msgOpt.value()));
                    } else {
                        break; // The queue is now empty.
                    }
                }
            }
            // Check if the batch became full during the inner loop.
            if (batch.size() >= maxBatchSize) {
                break;
            }
        }
    }
}

Optional<std::shared_ptr<Message>> ModuleBase::popFrom(const std::string& portName) {
    if (m_inputQueueMap.count(portName)) {
        auto msgOpt = m_inputQueueMap[portName]->tryPop();
        if (msgOpt) {
            // Use TRACE for high-frequency events to avoid spamming logs.
            LOG_TRACE("Module '{}': Popped message from input port '{}'.", m_name, portName);
            return msgOpt;
        }
        // It's normal for a queue to be empty, so no log here.
        return nullOpt;
    }
    LOG_WARN("Module '{}': Attempted to pop from non-existent input port '{}'.", m_name, portName);
    return nullOpt;
}

void ModuleBase::dispatchTo(const std::string& portName, const std::shared_ptr<Message>& msg) {
    if (m_outputQueueMap.count(portName)) {
        LOG_TRACE("Module '{}': Dispatching message to output port '{}'.", m_name, portName);
        m_outputQueueMap[portName]->push(std::move(msg));
    } else {
        LOG_WARN("Module '{}': Attempted to dispatch to non-existent output port '{}'.", m_name, portName);
    }
}

void ModuleBase::broadcast(std::shared_ptr<Message> msg) {
    LOG_TRACE("Module '{}': Broadcasting message to {} output ports.", m_name, m_outputQueueMap.size());
    for (const auto& item : m_outputQueueMap) {
        item.second->push(msg);
    }
}

void ModuleBase::Run() {
    LOG_DEBUG("Module '{}' run loop started, isSourceModule={}, isSinkModule={}", m_name, isSourceModule(), isSinkModule());

    if (isSourceModule()) {
        while (!m_stopFlag.load()) {
            Process(nullptr);
        }
        return;
    }

    // TODO: loading batch size and timeout from config.
    constexpr size_t BATCH_SIZE = 64;
    constexpr std::chrono::milliseconds BATCH_TIMEOUT(10);
    std::vector<std::shared_ptr<Message>> batchMessage;

    while (!m_stopFlag.load()) {
        try {
            collectBatch(batchMessage, BATCH_SIZE, BATCH_TIMEOUT);
            if (!batchMessage.empty()) {
                ProcessBatch(batchMessage);
            }
        } catch (const std::exception& e) {
            LOG_ERROR("Module '{}' caught an unhandled exception in its run loop: {}", m_name, e.what());
            // Depending on strategy, you might want to stop the module on error.
            // m_stopFlag = true;
        } catch (...) {
            LOG_CRITICAL("Module '{}' caught an unknown, non-standard exception! Terminating loop.", m_name);
            m_stopFlag = true;
        }
    }
    LOG_DEBUG("Module '{}' run loop finished.", m_name);
}

void ModuleBase::ProcessBatch(const std::vector<std::shared_ptr<Message>>& inputBatch) {
    for (auto& msg : inputBatch) {
        Process(msg);
    }
}
