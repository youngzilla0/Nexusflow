#include "executor/SchedulingPolicy.hpp"

#include <chrono>
#include <memory>

namespace nexusflow { namespace executor {

namespace {

constexpr std::size_t kSingleWorkerInputStepsPerTask = 16;
constexpr std::size_t kParallelInputStepsPerTask = 64;
constexpr std::size_t kSourceStepsPerTask = 1;

class DeterministicSingleWorkerPolicy final : public SchedulingPolicy {
public:
    const char* Name() const override { return "DeterministicSingleWorker"; }

    TaskExecutionPlan Plan(const SchedulingContext& context) const override {
        TaskExecutionPlan plan;
        plan.executionMode = ResolveExecutionMode(context);
        // 单 worker 场景优先保证公平性，限制单次连续执行步数。
        plan.maxStepsPerTask = context.isSourceActor ? kSourceStepsPerTask : kSingleWorkerInputStepsPerTask;
        return plan;
    }

    bool ShouldPrimeActorOnStart(const SchedulingContext& context, bool hasPendingWork) const override {
        // Polling source 在启动后需要主动进入调度循环。
        if (context.isSourceActor) {
            return context.sourcePolicy == Module::SourcePolicy::Polling;
        }
        // 普通 actor 仅在存在待处理工作时需要启动即入队。
        return hasPendingWork;
    }

    std::chrono::microseconds IdleBackoff(const SchedulingContext& context,
                                          const SchedulingFeedback& feedback) const override {
        if (!context.isSourceActor || context.sourcePolicy != Module::SourcePolicy::Polling) {
            return std::chrono::microseconds(0);
        }
        // 仅在本轮未产生输出时执行退避，以降低空转开销。
        if (feedback.emittedOutputs || context.idleWaitUs == 0) {
            return std::chrono::microseconds(0);
        }
        return std::chrono::microseconds(context.idleWaitUs);
    }

    bool ShouldReschedule(const SchedulingContext& context, const SchedulingFeedback& feedback) const override {
        // 单 worker 策略下，source 保持轮询，其余 actor 按信号或待处理工作续调度。
        if (context.isSourceActor && context.sourcePolicy == Module::SourcePolicy::Polling) {
            return true;
        }
        return feedback.hasPendingSignals || feedback.hasPendingWork;
    }

private:
    TaskExecutionMode ResolveExecutionMode(const SchedulingContext& context) const {
        if (context.isSourceActor) {
            return TaskExecutionMode::Source;
        }
        // Auto 当前内部统一映射为 OnAnyInput。
        const auto triggerPolicy =
            context.triggerPolicy == Module::TriggerPolicy::Auto ? Module::TriggerPolicy::OnAnyInput : context.triggerPolicy;
        return triggerPolicy == Module::TriggerPolicy::OnAllInputs ? TaskExecutionMode::OnAllInputs
                                                                   : TaskExecutionMode::OnAnyInput;
    }
};

class ThroughputOrientedPolicy final : public SchedulingPolicy {
public:
    const char* Name() const override { return "ThroughputOriented"; }

    TaskExecutionPlan Plan(const SchedulingContext& context) const override {
        TaskExecutionPlan plan;
        plan.executionMode = ResolveExecutionMode(context);
        // 多 worker 场景允许更大的步数预算，以降低重复入队开销。
        plan.maxStepsPerTask = context.isSourceActor ? kSourceStepsPerTask : kParallelInputStepsPerTask;
        return plan;
    }

    bool ShouldPrimeActorOnStart(const SchedulingContext& context, bool hasPendingWork) const override {
        if (context.isSourceActor) {
            return context.sourcePolicy == Module::SourcePolicy::Polling;
        }
        return hasPendingWork;
    }

    std::chrono::microseconds IdleBackoff(const SchedulingContext& context,
                                          const SchedulingFeedback& feedback) const override {
        if (!context.isSourceActor || context.sourcePolicy != Module::SourcePolicy::Polling) {
            return std::chrono::microseconds(0);
        }
        if (feedback.emittedOutputs || context.idleWaitUs == 0) {
            return std::chrono::microseconds(0);
        }
        return std::chrono::microseconds(context.idleWaitUs);
    }

    bool ShouldReschedule(const SchedulingContext& context, const SchedulingFeedback& feedback) const override {
        // 吞吐优先策略下，source 仍保持持续轮询。
        if (context.isSourceActor && context.sourcePolicy == Module::SourcePolicy::Polling) {
            return true;
        }
        return feedback.hasPendingSignals || feedback.hasPendingWork;
    }

private:
    TaskExecutionMode ResolveExecutionMode(const SchedulingContext& context) const {
        if (context.isSourceActor) {
            return TaskExecutionMode::Source;
        }
        const auto triggerPolicy =
            context.triggerPolicy == Module::TriggerPolicy::Auto ? Module::TriggerPolicy::OnAnyInput : context.triggerPolicy;
        return triggerPolicy == Module::TriggerPolicy::OnAllInputs ? TaskExecutionMode::OnAllInputs
                                                                   : TaskExecutionMode::OnAnyInput;
    }
};

} // namespace

std::unique_ptr<SchedulingPolicy> CreateSchedulingPolicy(std::size_t threadCount) {
    if (threadCount <= 1) {
        return std::make_unique<DeterministicSingleWorkerPolicy>();
    }
    return std::make_unique<ThroughputOrientedPolicy>();
}

}} // namespace nexusflow::executor
