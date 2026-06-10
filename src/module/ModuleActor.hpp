#ifndef NEXUSFLOW_MODULE_ACTOR_HPP
#define NEXUSFLOW_MODULE_ACTOR_HPP

#include "executor/Executor.hpp"
#include "nexusflow/ErrorCode.hpp"
#include <nexusflow/Module.hpp>

namespace nexusflow {

class ModuleActor {
public:
    ModuleActor(const std::shared_ptr<Module>& module,
                const PipelineConfig& runtimeConfig,
                const std::shared_ptr<PipelineContext>& pipelineContext,
                const std::shared_ptr<executor::Executor>& executor);

    ~ModuleActor();

    void AddInputQueue(const std::string& portName, ViewPtr<MessageQueue> queue,
                       const executor::Executor::PortRuntimeStatsStatePtr& stats) {
        m_executor->AddInputQueue(m_module->GetModuleName(), portName, queue, stats);
    }

    void AddOutputQueue(const std::string& outputPortName, const std::string& dstActorName, const std::string& dstInputPortName,
                        ViewPtr<MessageQueue> queue, const executor::Executor::PortRuntimeStatsStatePtr& stats) {
        m_executor->AddOutputQueue(m_module->GetModuleName(), outputPortName, dstActorName, dstInputPortName, queue, stats);
    }

    std::shared_ptr<Module>& GetModule() { return m_module; }

    std::string GetModuleName() const { return m_module->GetModuleName(); }

    ErrorCode Init();

    ErrorCode DeInit();

    ErrorCode Start();

    ErrorCode Stop();

private:
    std::shared_ptr<Module> m_module;
    std::shared_ptr<executor::Executor> m_executor;
};
} // namespace nexusflow

#endif
