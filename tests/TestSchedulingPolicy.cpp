#include <gtest/gtest.h>

#include "executor/SchedulingPolicy.hpp"

#include <chrono>
#include <memory>

using namespace nexusflow;
using namespace nexusflow::executor;

namespace {

TEST(SchedulingPolicyTest, SingleWorkerPolicy_PrefersFairOnAnyInputExecution) {
    auto policy = CreateSchedulingPolicy(1);
    ASSERT_NE(policy, nullptr);
    EXPECT_STREQ(policy->Name(), "DeterministicSingleWorker");

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::Auto;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAnyInput);
    EXPECT_EQ(plan.maxStepsPerTask, 16u);

    EXPECT_FALSE(policy->ShouldPrimeActorOnStart(context, false));
    EXPECT_TRUE(policy->ShouldPrimeActorOnStart(context, true));

    SchedulingFeedback feedback;
    feedback.hasPendingSignals = true;
    EXPECT_TRUE(policy->ShouldReschedule(context, feedback));
}

TEST(SchedulingPolicyTest, MultiWorkerPolicy_UsesJoinModeAndSourceIdleBackoff) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);
    EXPECT_STREQ(policy->Name(), "ThroughputOriented");

    SchedulingContext joinContext;
    joinContext.isSourceActor = false;
    joinContext.triggerPolicy = Module::TriggerPolicy::OnAllInputs;

    const auto joinPlan = policy->Plan(joinContext);
    EXPECT_EQ(joinPlan.executionMode, TaskExecutionMode::OnAllInputs);
    EXPECT_EQ(joinPlan.maxStepsPerTask, 64u);

    SchedulingContext sourceContext;
    sourceContext.isSourceActor = true;
    sourceContext.sourcePolicy = Module::SourcePolicy::Polling;
    sourceContext.idleWaitUs = 25;

    const auto sourcePlan = policy->Plan(sourceContext);
    EXPECT_EQ(sourcePlan.executionMode, TaskExecutionMode::Source);
    EXPECT_EQ(sourcePlan.maxStepsPerTask, 1u);
    EXPECT_TRUE(policy->ShouldPrimeActorOnStart(sourceContext, false));

    SchedulingFeedback idleFeedback;
    idleFeedback.completedSteps = 1;
    idleFeedback.madeProgress = true;
    idleFeedback.emittedOutputs = false;
    EXPECT_EQ(policy->IdleBackoff(sourceContext, idleFeedback), std::chrono::microseconds(25));

    SchedulingFeedback busyFeedback = idleFeedback;
    busyFeedback.emittedOutputs = true;
    EXPECT_EQ(policy->IdleBackoff(sourceContext, busyFeedback), std::chrono::microseconds(0));

    EXPECT_TRUE(policy->ShouldReschedule(sourceContext, busyFeedback));
}

} // namespace
