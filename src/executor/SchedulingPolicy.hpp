#ifndef NEXUSFLOW_EXECUTOR_SCHEDULING_POLICY_HPP
#define NEXUSFLOW_EXECUTOR_SCHEDULING_POLICY_HPP

#include <nexusflow/Module.hpp>

#include <cstddef>
#include <memory>

namespace nexusflow { namespace executor {

// 调度策略只关心三件事：
// 1. 一个 actor 单次拿到 worker 后最多跑多少步
// 2. Auto 触发在运行时应落到哪种具体模式
// 3. 本轮执行结束后是否应该立即重新入队
struct SchedulingContext {
    bool isSourceActor = false;
    Module::SourcePolicy sourcePolicy = Module::SourcePolicy::Polling;
    Module::TriggerPolicy triggerPolicy = Module::TriggerPolicy::Auto;
    bool hasPendingSignals = false;
    bool hasPendingWork = false;
};

class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;

    virtual const char* Name() const = 0;
    virtual std::size_t MaxStepsPerTask(const SchedulingContext& context) const = 0;
    virtual Module::TriggerPolicy ResolveTriggerPolicy(const SchedulingContext& context) const = 0;
    virtual bool ShouldReschedule(const SchedulingContext& context) const = 0;
};

// 根据线程数选择内部调度策略。
// 目前不暴露成公开 API，先把策略边界从 Executor 主流程里拆出来。
std::unique_ptr<SchedulingPolicy> CreateSchedulingPolicy(std::size_t threadCount);

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_SCHEDULING_POLICY_HPP
