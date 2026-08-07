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
    EXPECT_EQ(plan.maxStepsPerTask, 1u);

    EXPECT_FALSE(policy->ShouldPrimeActorOnStart(context, false));
    EXPECT_TRUE(policy->ShouldPrimeActorOnStart(context, true));

    SchedulingFeedback feedback;
    feedback.hasPendingSignals = true;
    EXPECT_TRUE(policy->ShouldReschedule(context, feedback));

    SchedulingFeedback idleFeedback;
    EXPECT_EQ(policy->IdleBackoff(context, idleFeedback), std::chrono::microseconds(0));
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

TEST(SchedulingPolicyTest, ForkJoinTopology_ReducesPerTaskBudget) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAnyInput;
    GraphTopologyInfo topology;
    topology.forkJoinGroups.push_back(GraphTopologyInfo::ForkJoinGroup{});
    context.topologyInfo = &topology;
    context.forkJoinGroupCount = topology.forkJoinGroups.size();

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAnyInput);
    EXPECT_EQ(plan.maxStepsPerTask, 32u);
}

TEST(SchedulingPolicyTest, JoinAndHighBranchTopology_FurtherReduceTaskBudget) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAllInputs;
    context.branchCount = 4;
    context.joinCount = 1;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAllInputs);
    EXPECT_EQ(plan.maxStepsPerTask, 16u);
}

TEST(SchedulingPolicyTest, LocalJoinRole_ReducesBudgetEvenWithoutGlobalCounts) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAllInputs;
    context.isJoinActor = true;
    context.localJoinFanIn = 2;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAllInputs);
    EXPECT_EQ(plan.maxStepsPerTask, 32u);
}

TEST(SchedulingPolicyTest, LocalBranchFanOut_ReducesBudgetForBranchActor) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAnyInput;
    context.isBranchActor = true;
    context.localBranchFanOut = 4;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAnyInput);
    EXPECT_EQ(plan.maxStepsPerTask, 32u);
}

TEST(SchedulingPolicyTest, SourceActor_DoesNotUseTopologyBudgetReduction) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = true;
    context.sourcePolicy = Module::SourcePolicy::Polling;
    context.triggerPolicy = Module::TriggerPolicy::OnAllInputs;
    context.isJoinActor = true;
    context.isBranchActor = true;
    context.isForkJoinActor = true;
    context.localBranchFanOut = 8;
    context.localJoinFanIn = 8;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::Source);
    EXPECT_EQ(plan.maxStepsPerTask, 1u);
}

TEST(SchedulingPolicyTest, BranchJoinCombination_UsesIntermediateBudgetTier) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAllInputs;
    context.isBranchActor = true;
    context.isJoinActor = true;
    context.localBranchFanOut = 4;
    context.localJoinFanIn = 2;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAllInputs);
    EXPECT_EQ(plan.maxStepsPerTask, 16u);
}

TEST(SchedulingPolicyTest, ForkJoinJoinCombination_UsesMoreConservativeBudgetTier) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAllInputs;
    context.isForkJoinActor = true;
    context.isJoinActor = true;
    context.localJoinFanIn = 2;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAllInputs);
    EXPECT_EQ(plan.maxStepsPerTask, 16u);
}

TEST(SchedulingPolicyTest, ForkJoinBranchJoinCombination_UsesLowestBudgetTier) {
    auto policy = CreateSchedulingPolicy(4);
    ASSERT_NE(policy, nullptr);

    SchedulingContext context;
    context.isSourceActor = false;
    context.triggerPolicy = Module::TriggerPolicy::OnAllInputs;
    context.isForkJoinActor = true;
    context.isBranchActor = true;
    context.isJoinActor = true;
    context.localBranchFanOut = 4;
    context.localJoinFanIn = 2;

    const auto plan = policy->Plan(context);
    EXPECT_EQ(plan.executionMode, TaskExecutionMode::OnAllInputs);
    EXPECT_EQ(plan.maxStepsPerTask, 8u);
}

} // namespace
