#include "ModuleBase.hpp" // Adjust path as necessary
#include "ErrorCode.hpp"
#include "helper/logging.hpp"

ModuleBase::ModuleBase(std::string name, bool isSourceModule)
    : m_name(std::move(name)), m_isSourceModule(isSourceModule), m_stopFlag(false) {
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

std::shared_ptr<Message> ModuleBase::selectMessage() {
    // TODO: Implement a more sophisticated message selection mechanism if needed.
    std::shared_ptr<Message> message = nullptr;
    for (auto& item : m_inputQueueMap) {
        auto& queueName = item.first;
        auto& queue = item.second;
        auto msgOpt = queue->tryPop();
        if (msgOpt) {
            LOG_TRACE("Module '{}': Selected message from input port '{}'.", m_name, queueName);
            message = msgOpt.value();
            break;
        }
    }
    return message;
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
    LOG_DEBUG("Module '{}' run loop started.", m_name);

    while (!m_stopFlag.load()) {
        try {
            // If this module is a source module, it should always process.
            if (isSourceModule()) {
                Process(nullptr);
                continue;
            }

            // If this module is not a source module, it should select a message to process.
            auto message = selectMessage();
            if (message != nullptr) {
                Process(message);
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(5)); // Sleep briefly to avoid busy-waiting.
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

void ModuleBase::Process(const std::shared_ptr<Message>& inputMessage) {
    //
    LOG_DEBUG("Module '{}' processing...", m_name);
}
