#include "nexusflow/Module.hpp"
#include "dispatcher/Dispatcher.hpp"
#include "nexusflow/Config.hpp"
#include "nexusflow/ErrorCode.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/ProcessingContext.hpp"
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

ErrorCode Module::Configure(const Config& config) {
    // Initialize the module.
    LOG_TRACE("Module '{}' initializing...", m_moduleName);
    return ErrorCode::SUCCESS;
}

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

std::vector<ProcessStatus> Module::ProcessBatch(std::vector<ProcessingContext>& inputBatchContexts) {
    // Process the batch of input messages.
    LOG_DEBUG("Module '{}' processing batch of {} messages.", m_moduleName, inputBatchContexts.size());
    std::vector<ProcessStatus> outputBatchStatuses;
    outputBatchStatuses.reserve(inputBatchContexts.size());
    for (auto& context : inputBatchContexts) {
        outputBatchStatuses.push_back(Process(context));
    }
    return outputBatchStatuses;
}

const std::string& Module::GetModuleName() const { return m_moduleName; }

} // namespace nexusflow
