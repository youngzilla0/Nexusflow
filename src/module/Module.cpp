#include "nexusflow/Module.hpp"

#include "executor/Executor.hpp"
#include "utils/logging.hpp"

#include <stdexcept>
#include <utility>

namespace nexusflow {

Module::Module(std::string name) : m_moduleName(std::move(name)) { LOG_TRACE("Module '{}' created.", m_moduleName); }

Module::~Module() { LOG_TRACE("Module '{}' destroying...", m_moduleName); }

ErrorCode Module::Configure(const Config& config) {
    (void)config;
    LOG_TRACE("Module '{}' configuring...", m_moduleName);
    return ErrorCode::SUCCESS;
}

ErrorCode Module::Init() {
    LOG_TRACE("Module '{}' initializing...", m_moduleName);
    return ErrorCode::SUCCESS;
}

ErrorCode Module::DeInit() {
    LOG_TRACE("Module '{}' de-initializing...", m_moduleName);
    return ErrorCode::SUCCESS;
}

const PipelineContext& Module::GetPipelineContext() const {
    if (m_pipelineContext == nullptr) {
        throw std::runtime_error("PipelineContext is not available for module '" + m_moduleName + "'");
    }
    return *m_pipelineContext;
}

void Module::SetPipelineContext(const std::shared_ptr<PipelineContext>& pipelineContext) { m_pipelineContext = pipelineContext; }

void Module::Broadcast(const Message& message, bool blocking) {
    if (m_executor != nullptr) {
        m_executor->Emit(m_moduleName, message, blocking);
    }
}

void Module::SendTo(const std::string& outputPortName, const Message& message, bool blocking) {
    if (m_executor != nullptr) {
        m_executor->Route(m_moduleName, outputPortName, message, blocking);
    }
}

void Module::SetExecutor(const std::shared_ptr<executor::Executor>& executor) { m_executor = executor; }

} // namespace nexusflow
