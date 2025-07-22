#include "ModuleActor.hpp"
#include "common/ViewPtr.hpp"
#include "module/ActorContext.hpp"
#include "nexusflow/ErrorCode.hpp"
#include <memory>

namespace nexusflow {

ModuleActor::ModuleActor(const std::shared_ptr<Module>& module, const Config& config,
                         const ViewPtr<profiling::ProfilerRegistry>& profilerRegistry) {
    m_module = module;
    m_config = std::make_unique<Config>(config);

    ViewPtr<Module> moduleView{m_module.get()};
    ViewPtr<Config> configView{m_config.get()};

    // Create actor context
    ActorContext actorContext;
    actorContext.config = configView;
    actorContext.profilerRegistry = profilerRegistry;

    m_worker = std::make_shared<core::Worker>(m_module, actorContext);
    m_dispatcher = std::make_shared<dispatcher::Dispatcher>(actorContext);

    m_worker->SetDispatcher(makeViewPtr(m_dispatcher.get()));
}

ModuleActor::~ModuleActor() = default;

ErrorCode ModuleActor::Init() { return m_module->Init(); }

ErrorCode ModuleActor::DeInit() { return m_module->DeInit(); }

ErrorCode ModuleActor::Start() {
    m_workThread = std::thread([this]() { m_worker->WorkLoop(); });

    return ErrorCode::SUCCESS;
}

ErrorCode ModuleActor::Stop() {
    m_worker->Stop();
    if (m_workThread.joinable()) {
        m_workThread.join();
    }
    return ErrorCode::SUCCESS;
}

} // namespace nexusflow
