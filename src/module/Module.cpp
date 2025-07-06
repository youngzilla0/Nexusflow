#include "nexusflow/Module.hpp" // Adjust path as necessary
#include "dispatcher/Dispatcher.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "utils/logging.hpp"

namespace nexusflow {

Module::Module(std::string name) : m_moduleName(std::move(name)) {
    // Initialize the module with the given name.
    LOG_TRACE("Module '{}' created.", m_moduleName);
}

Module::~Module() {
    // Ensure the module is stopped before destruction.
    LOG_TRACE("Module '{}' destroying...", m_moduleName);
};

ErrorCode Module::Init() {
    // Initialize the module.
    LOG_TRACE("Module '{}' initializing...", m_moduleName);
    return ErrorCode::SUCCESS;
}

ErrorCode Module::DeInit() {
    // De-initialize the module.
    LOG_TRACE("Module '{}' de-initializing...", m_moduleName);
    return ErrorCode::SUCCESS;
}

void Module::ProcessBatch(const std::vector<std::shared_ptr<Message>>& inputBatchMessages) {
    // Process the batch of input messages.
    LOG_DEBUG("Module '{}' processing batch of {} messages.", m_moduleName, inputBatchMessages.size());
    for (const auto& message : inputBatchMessages) {
        Process(message);
    }
}

void Module::Broadcast(const std::shared_ptr<Message>& message) {
    if (m_dispatcherPtr != nullptr) {
        LOG_DEBUG("Module '{}' broadcasting message.", m_moduleName);
        m_dispatcherPtr->Broadcast(message);
    } else {
        LOG_WARN("Module '{}' has no handle, cannot broadcast message.", m_moduleName);
    }
}

const std::string& Module::GetModuleName() const { return m_moduleName; }

void Module::SetDispatcher(const std::shared_ptr<dispatcher::Dispatcher>& dispatcher) { m_dispatcherPtr = dispatcher; }

} // namespace nexusflow
