#include "executor/SchedulingPolicy.hpp"

#include <algorithm>
#include <memory>

namespace nexusflow { namespace executor {

namespace {

constexpr std::size_t kSingleWorkerInputStepsPerTask = 16;
constexpr std::size_t kParallelInputStepsPerTask = 64;
constexpr std::size_t kSourceStepsPerTask = 1;

class DeterministicSingleWorkerPolicy final : public SchedulingPolicy {
public:
    const char* Name() const override { return "DeterministicSingleWorker"; }

    std::size_t MaxStepsPerTask(const SchedulingContext& context) const override {
        // 单 worker 时优先保证公平性，避免一个 actor 长时间占住唯一线程。
        return context.isSourceActor ? kSourceStepsPerTask : kSingleWorkerInputStepsPerTask;
    }

    Module::TriggerPolicy ResolveTriggerPolicy(const SchedulingContext& context) const override {
        return context.triggerPolicy == Module::TriggerPolicy::Auto ? Module::TriggerPolicy::OnAnyInput : context.triggerPolicy;
    }

    bool ShouldReschedule(const SchedulingContext& context) const override {
        // 单 worker 策略下，只要还有信号或队列里还有活，就尽快回到线程池尾部排队。
        if (context.isSourceActor && context.sourcePolicy == Module::SourcePolicy::Polling) {
            return true;
        }
        return context.hasPendingSignals || context.hasPendingWork;
    }
};

class ThroughputOrientedPolicy final : public SchedulingPolicy {
public:
    const char* Name() const override { return "ThroughputOriented"; }

    std::size_t MaxStepsPerTask(const SchedulingContext& context) const override {
        // 多 worker 时允许单次多跑几步，减少频繁入队带来的线程池调度开销。
        return context.isSourceActor ? kSourceStepsPerTask : kParallelInputStepsPerTask;
    }

    Module::TriggerPolicy ResolveTriggerPolicy(const SchedulingContext& context) const override {
        return context.triggerPolicy == Module::TriggerPolicy::Auto ? Module::TriggerPolicy::OnAnyInput : context.triggerPolicy;
    }

    bool ShouldReschedule(const SchedulingContext& context) const override {
        if (context.isSourceActor && context.sourcePolicy == Module::SourcePolicy::Polling) {
            return true;
        }
        return context.hasPendingSignals || context.hasPendingWork;
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
