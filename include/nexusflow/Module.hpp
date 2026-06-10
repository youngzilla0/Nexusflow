#ifndef NEXUSFLOW_MODULE_HPP
#define NEXUSFLOW_MODULE_HPP

#include <nexusflow/Config.hpp>
#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/Ports.hpp>

#include <memory>
#include <string>

namespace nexusflow {

class ModuleActor;
namespace executor {
class Executor;
}

/**
 * @class Module
 * @brief An abstract base class for a processing unit within a data pipeline.
 *
 * A Module focuses exclusively on business logic. It receives inputs through a
 * port-oriented view and emits outputs through port-oriented commands, while
 * the framework owns scheduling, queues, and thread management.
 */
class Module {
public:
    enum class TriggerPolicy { Auto, OnAnyInput, OnAllInputs };

public:
    explicit Module(std::string name);
    virtual ~Module();

    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    virtual ErrorCode Configure(const Config& config);
    virtual ErrorCode Init();
    virtual ErrorCode DeInit();

    virtual void Process(const PortInputsView& inputs, PortOutputs& outputs) = 0;

    TriggerPolicy GetTriggerPolicy() const { return m_triggerPolicy; }

    void SetTriggerPolicy(TriggerPolicy policy) { m_triggerPolicy = policy; }

    const std::string& GetModuleName() const { return m_moduleName; }

    const PipelineContext& GetPipelineContext() const;

protected:
    void Broadcast(const Message& message, bool blocking);

    void SendTo(const std::string& outputPortName, const Message& message, bool blocking);

private:
    friend class ModuleActor;

    void SetExecutor(const std::shared_ptr<executor::Executor>& executor);
    void SetPipelineContext(const std::shared_ptr<PipelineContext>& pipelineContext);

    std::string m_moduleName;
    std::shared_ptr<executor::Executor> m_executor;
    std::shared_ptr<PipelineContext> m_pipelineContext;
    TriggerPolicy m_triggerPolicy = TriggerPolicy::Auto;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MODULE_HPP
