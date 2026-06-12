#include "ModuleNode.hpp"
#include "nexusflow/ErrorCode.hpp"

#include <memory>

namespace nexusflow {

ModuleNode::ModuleNode(const std::shared_ptr<Module>& module,
                       const PipelineConfig& runtimeConfig,
                       const std::shared_ptr<PipelineContext>& pipelineContext,
                       const std::shared_ptr<executor::Executor>& executor) {
    m_module = module;
    m_executor = executor;
    m_module->SetExecutor(m_executor);
    m_module->SetPipelineContext(pipelineContext);
    m_executor->RegisterNode(m_module->GetModuleName(), m_module, runtimeConfig);
}

ModuleNode::~ModuleNode() = default;

ErrorCode ModuleNode::Init() { return m_module->Init(); }

ErrorCode ModuleNode::DeInit() { return m_module->DeInit(); }

ErrorCode ModuleNode::Start() {
    m_executor->Start();
    return ErrorCode::SUCCESS;
}

ErrorCode ModuleNode::Stop() {
    m_executor->Stop();
    return ErrorCode::SUCCESS;
}

} // namespace nexusflow
