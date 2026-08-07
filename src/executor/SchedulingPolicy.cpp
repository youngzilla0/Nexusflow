#include "executor/SchedulingPolicy.hpp"

#include <chrono>
#include <memory>

namespace nexusflow { namespace executor {

namespace {

constexpr std::size_t kParallelInputStepsPerTask = 64;
constexpr std::size_t kSourceStepsPerTask = 1;
constexpr std::size_t kForkJoinInputStepsPerTask = 32;
constexpr std::size_t kJoinOrBranchInputStepsPerTask = 32;
constexpr std::size_t kForkJoinMixedInputStepsPerTask = 16;
constexpr std::size_t kBranchJoinInputStepsPerTask = 16;
constexpr std::size_t kForkJoinBranchJoinInputStepsPerTask = 8;

TaskExecutionMode ResolveExecutionMode(const SchedulingContext& context) {
    if (context.isSourceActor) {
        return TaskExecutionMode::Source;
    }

    const auto triggerPolicy =
        context.triggerPolicy == Module::TriggerPolicy::Auto ? Module::TriggerPolicy::OnAnyInput : context.triggerPolicy;
    return triggerPolicy == Module::TriggerPolicy::OnAllInputs ? TaskExecutionMode::OnAllInputs
                                                               : TaskExecutionMode::OnAnyInput;
}

bool IsJoinLikeActor(const SchedulingContext& context) {
    return context.isJoinActor || context.localJoinFanIn >= 2 || context.joinCount > 0;
}

bool IsWideBranchActor(const SchedulingContext& context) {
    if (context.isBranchActor) {
        return context.localBranchFanOut >= 4;
    }
    return context.branchCount >= 4;
}

bool IsForkJoinSensitiveActor(const SchedulingContext& context) {
    return context.isForkJoinActor || context.forkJoinGroupCount > 0;
}

struct ActorRoleProfile {
    bool isSource = false;
    bool isJoinLike = false;
    bool isWideBranch = false;
    bool isForkJoinSensitive = false;
};

ActorRoleProfile BuildActorRoleProfile(const SchedulingContext& context) {
    ActorRoleProfile profile;
    profile.isSource = context.isSourceActor;
    profile.isJoinLike = IsJoinLikeActor(context);
    profile.isWideBranch = IsWideBranchActor(context);
    profile.isForkJoinSensitive = IsForkJoinSensitiveActor(context);
    return profile;
}

std::size_t ResolveThroughputInputBudget(const SchedulingContext& context) {
    const auto profile = BuildActorRoleProfile(context);
    if (profile.isSource) {
        return kSourceStepsPerTask;
    }

    // 预算按节点角色分档，而不是按条件逐步相乘，便于直接解释：
    // Linear(64) -> Branch/Join/ForkJoin(32) -> 组合敏感角色(16) -> ForkJoin+Branch+Join(8)。
    if (profile.isForkJoinSensitive && profile.isJoinLike && profile.isWideBranch) {
        return kForkJoinBranchJoinInputStepsPerTask;
    }
    if (profile.isForkJoinSensitive && (profile.isJoinLike || profile.isWideBranch)) {
        return kForkJoinMixedInputStepsPerTask;
    }
    if (profile.isJoinLike && profile.isWideBranch) {
        return kBranchJoinInputStepsPerTask;
    }
    if (profile.isForkJoinSensitive) {
        return kForkJoinInputStepsPerTask;
    }
    if (profile.isJoinLike || profile.isWideBranch) {
        return kJoinOrBranchInputStepsPerTask;
    }
    return kParallelInputStepsPerTask;
}

class DeterministicSingleWorkerPolicy final : public SchedulingPolicy {
public:
    const char* Name() const override { return "DeterministicSingleWorker"; }

    TaskExecutionPlan Plan(const SchedulingContext& context) const override {
        TaskExecutionPlan plan;
        plan.executionMode = ::nexusflow::executor::ResolveExecutionMode(context);
        // 单 worker 场景优先保证公平性，默认每次只跑一个 step，避免某个 actor 长时间独占线程。
        plan.maxStepsPerTask = context.isSourceActor ? kSourceStepsPerTask : 1;
        if (!context.isSourceActor && IsForkJoinSensitiveActor(context)) {
            plan.maxStepsPerTask = 1;
        }
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
            return feedback.emittedOutputs ? std::chrono::microseconds(0) : std::chrono::microseconds(1);
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
};

class ThroughputOrientedPolicy final : public SchedulingPolicy {
public:
    const char* Name() const override { return "ThroughputOriented"; }

    TaskExecutionPlan Plan(const SchedulingContext& context) const override {
        TaskExecutionPlan plan;
        plan.executionMode = ::nexusflow::executor::ResolveExecutionMode(context);
        // 多 worker 场景允许更大的步数预算，以降低重复入队开销。
        plan.maxStepsPerTask = context.isSourceActor ? kSourceStepsPerTask : ResolveThroughputInputBudget(context);
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
};

} // namespace

std::unique_ptr<SchedulingPolicy> CreateSchedulingPolicy(std::size_t threadCount) {
    if (threadCount <= 1) {
        return std::make_unique<DeterministicSingleWorkerPolicy>();
    }
    return std::make_unique<ThroughputOrientedPolicy>();
}

}} // namespace nexusflow::executor
