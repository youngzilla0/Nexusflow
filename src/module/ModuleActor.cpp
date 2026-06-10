#include "ModuleActor.hpp"
#include "nexusflow/ErrorCode.hpp"

#include <memory>

namespace nexusflow {

ModuleActor::ModuleActor(const std::shared_ptr<Module>& module,
                         const PipelineConfig& runtimeConfig,
                         const std::shared_ptr<PipelineContext>& pipelineContext,
                         const std::shared_ptr<executor::Executor>& executor) {
    m_module = module;
    m_executor = executor;
    m_module->SetExecutor(m_executor);
    m_module->SetPipelineContext(pipelineContext);
    m_executor->RegisterActor(m_module->GetModuleName(), m_module, runtimeConfig);
}

ModuleActor::~ModuleActor() = default;

ErrorCode ModuleActor::Init() { return m_module->Init(); }

ErrorCode ModuleActor::DeInit() { return m_module->DeInit(); }

ErrorCode ModuleActor::Start() {
    m_executor->Start();
    return ErrorCode::SUCCESS;
}

ErrorCode ModuleActor::Stop() {
    m_executor->Stop();
    return ErrorCode::SUCCESS;
}

} // namespace nexusflow
