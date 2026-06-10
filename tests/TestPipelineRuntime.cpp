#include <gtest/gtest.h>

#include "executor/Executor.hpp"

#include <nexusflow/Nexusflow.hpp>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace nexusflow;
using namespace std::chrono_literals;

namespace {

class ManualSourceModule : public Module {
public:
    explicit ManualSourceModule(std::string name) : Module(std::move(name)) { SetSourcePolicy(SourcePolicy::Manual); }

    void Send(int value, bool blocking) { Broadcast(MakeMessage(value, GetModuleName()), blocking); }

    void SendMessage(Message message, bool blocking) { Broadcast(message, blocking); }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }
};

class CollectSinkModule : public Module {
public:
    explicit CollectSinkModule(std::string name) : Module(std::move(name)) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;

        auto* value = inputs.OnlyAs<int>();
        if (value == nullptr) {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_values.push_back(*value);
        }
        m_cond.notify_all();
    }

    bool WaitForCount(std::size_t expectedCount, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cond.wait_for(lock, timeout, [this, expectedCount]() { return m_values.size() >= expectedCount; });
    }

    std::vector<int> Values() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_values;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::vector<int> m_values;
};

class ForwardModule : public Module {
public:
    explicit ForwardModule(std::string name) : Module(std::move(name)) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        auto* message = inputs.OnlyMessage();
        if (message == nullptr) {
            return;
        }
        outputs.Emit(*message, true);
    }
};

class ContextAwareForwardModule : public Module {
public:
    explicit ContextAwareForwardModule(std::string name) : Module(std::move(name)) {}

    ErrorCode Init() override {
        m_statisticsEnabled = GetPipelineContext().IsStatisticsEnabled();
        m_throughputEnabled = GetPipelineContext().IsThroughputStatisticsEnabled();
        m_latencyEnabled = GetPipelineContext().IsLatencyStatisticsEnabled();
        return ErrorCode::SUCCESS;
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        auto* message = inputs.OnlyMessage();
        if (message == nullptr) {
            return;
        }
        outputs.Emit(*message, true);
    }

    bool StatisticsEnabled() const { return m_statisticsEnabled; }
    bool ThroughputEnabled() const { return m_throughputEnabled; }
    bool LatencyEnabled() const { return m_latencyEnabled; }

private:
    bool m_statisticsEnabled = true;
    bool m_throughputEnabled = true;
    bool m_latencyEnabled = true;
};

class JoinSumModule : public Module {
public:
    explicit JoinSumModule(std::string name) : Module(std::move(name)) { SetTriggerPolicy(TriggerPolicy::OnAllInputs); }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        auto* left = inputs.Get<int>("left");
        auto* right = inputs.Get<int>("right");
        if (left == nullptr || right == nullptr) {
            return;
        }
        outputs.Emit(MakeMessage((*left) + (*right), GetModuleName()), true);
    }
};

PortRuntimeStats GetOnlyPortStats(const Pipeline& pipeline) {
    auto stats = pipeline.GetPortStats();
    EXPECT_EQ(stats.size(), 1u);
    return stats.empty() ? PortRuntimeStats{} : stats.front();
}

const ActorRuntimeStats* FindActorStats(const PipelineObservation& observation, const std::string& actorName) {
    for (const auto& actor : observation.actors) {
        if (actor.actorName == actorName) {
            return &actor;
        }
    }
    return nullptr;
}

} // namespace

TEST(PipelineRuntimeTest, NonBlockingDropTail_DropsNewestAndPreservesOldestMessage) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 1;
    config.idleWaitUs = 10;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = PipelineBuilder().AddModule(source).AddModule(sink).Connect("Source", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);

    source->Send(1, false);
    source->Send(2, false);
    source->Send(3, false);

    auto beforeStart = GetOnlyPortStats(*pipeline);
    EXPECT_EQ(beforeStart.pushAttempts, 3u);
    EXPECT_EQ(beforeStart.blockingPushAttempts, 0u);
    EXPECT_EQ(beforeStart.nonBlockingPushAttempts, 3u);
    EXPECT_EQ(beforeStart.enqueueCount, 1u);
    EXPECT_EQ(beforeStart.dropCount, 2u);
    EXPECT_EQ(beforeStart.rejectCount, 0u);
    EXPECT_EQ(beforeStart.dequeueCount, 0u);
    EXPECT_EQ(beforeStart.currentDepth, 1u);
    EXPECT_EQ(beforeStart.peakDepth, 1u);

    ASSERT_EQ(pipeline->Start(), ErrorCode::SUCCESS);
    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({1}));

    auto afterStart = GetOnlyPortStats(*pipeline);
    EXPECT_EQ(afterStart.dequeueCount, 1u);
    EXPECT_EQ(afterStart.currentDepth, 0u);

    EXPECT_EQ(pipeline->Stop(), ErrorCode::SUCCESS);
    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, NonBlockingDropHead_DropsOldestAndPreservesLatestMessage) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 1;
    config.idleWaitUs = 10;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropHead;

    auto pipeline = PipelineBuilder().AddModule(source).AddModule(sink).Connect("Source", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);

    source->Send(1, false);
    source->Send(2, false);
    source->Send(3, false);

    auto beforeStart = GetOnlyPortStats(*pipeline);
    EXPECT_EQ(beforeStart.pushAttempts, 3u);
    EXPECT_EQ(beforeStart.blockingPushAttempts, 0u);
    EXPECT_EQ(beforeStart.nonBlockingPushAttempts, 3u);
    EXPECT_EQ(beforeStart.enqueueCount, 3u);
    EXPECT_EQ(beforeStart.dropCount, 2u);
    EXPECT_EQ(beforeStart.rejectCount, 0u);
    EXPECT_EQ(beforeStart.dequeueCount, 0u);
    EXPECT_EQ(beforeStart.currentDepth, 1u);
    EXPECT_EQ(beforeStart.peakDepth, 1u);

    ASSERT_EQ(pipeline->Start(), ErrorCode::SUCCESS);
    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({3}));

    auto afterStart = GetOnlyPortStats(*pipeline);
    EXPECT_EQ(afterStart.dequeueCount, 1u);
    EXPECT_EQ(afterStart.currentDepth, 0u);

    EXPECT_EQ(pipeline->Stop(), ErrorCode::SUCCESS);
    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, ExecutorThreadPool_ReusesThreadsAcrossMultipleActors) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto pass = std::make_shared<ForwardModule>("Pass");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.executorThreadCount = 1;
    config.queueSize = 8;
    config.idleWaitUs = 10;

    auto pipeline =
        PipelineBuilder().AddModule(source).AddModule(pass).AddModule(sink).Connect("Source", "Pass").Connect("Pass", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);
    ASSERT_EQ(pipeline->Start(), ErrorCode::SUCCESS);

    source->Send(42, true);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({42}));

    EXPECT_EQ(pipeline->Stop(), ErrorCode::SUCCESS);
    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, ManualSource_DoesNotStarveSingleWorkerExecutor) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto pass1 = std::make_shared<ForwardModule>("Pass1");
    auto pass2 = std::make_shared<ForwardModule>("Pass2");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.executorThreadCount = 1;
    config.queueSize = 16;
    config.idleWaitUs = 0;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Pass1", "Pass2")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);
    ASSERT_EQ(pipeline->Start(), ErrorCode::SUCCESS);

    std::this_thread::sleep_for(20ms);
    source->Send(42, true);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({42}));

    EXPECT_EQ(pipeline->Stop(), ErrorCode::SUCCESS);
    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, StatisticsCanBeDisabledFromPipelineContext) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto pass = std::make_shared<ContextAwareForwardModule>("Pass");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 8;
    config.idleWaitUs = 10;
    config.statistics.enableStatistics = false;
    config.statistics.enableThroughput = true;
    config.statistics.enableLatency = true;

    auto pipeline =
        PipelineBuilder().AddModule(source).AddModule(pass).AddModule(sink).Connect("Source", "Pass").Connect("Pass", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);
    EXPECT_FALSE(pass->StatisticsEnabled());
    EXPECT_FALSE(pass->ThroughputEnabled());
    EXPECT_FALSE(pass->LatencyEnabled());

    source->Send(7, true);

    ASSERT_EQ(pipeline->Start(), ErrorCode::SUCCESS);
    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({7}));
    EXPECT_TRUE(pipeline->GetPortStats().empty());
    EXPECT_TRUE(pipeline->GetActorStats().empty());

    EXPECT_EQ(pipeline->Stop(), ErrorCode::SUCCESS);
    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, OnAllInputsJoinLimit_EvictsOldestPendingGroup) {
    auto leftSource = std::make_shared<ManualSourceModule>("Left");
    auto rightSource = std::make_shared<ManualSourceModule>("Right");
    auto join = std::make_shared<JoinSumModule>("Join");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 8;
    config.idleWaitUs = 10;
    config.fusionTimeoutMs = 60000;
    config.maxPendingJoinGroups = 1;

    auto pipeline = PipelineBuilder()
                        .AddModule(leftSource)
                        .AddModule(rightSource)
                        .AddModule(join)
                        .AddModule(sink)
                        .Connect("Left", "out", "Join", "left")
                        .Connect("Right", "out", "Join", "right")
                        .Connect("Join", "Sink")
                        .WithConfig(config)
                        .Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);

    auto sendTagged = [](const std::shared_ptr<ManualSourceModule>& source, int value, std::uint64_t messageId) {
        auto message = MakeMessage(value, source->GetModuleName());
        const auto baseTimestampMs =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::system_clock::now().time_since_epoch())
                                           .count());
        message.MetaData().messageId = messageId;
        message.MetaData().timestamp = baseTimestampMs + messageId;
        source->SendMessage(std::move(message), true);
    };

    sendTagged(leftSource, 10, 1);
    sendTagged(leftSource, 20, 2);

    ASSERT_EQ(pipeline->Start(), ErrorCode::SUCCESS);
    std::this_thread::sleep_for(20ms);

    PipelineObserver observerBeforeJoin(*pipeline);
    const auto observationBeforeJoin = observerBeforeJoin.Snapshot();
    const auto* joinStatsBeforeJoin = FindActorStats(observationBeforeJoin, "Join");
    ASSERT_NE(joinStatsBeforeJoin, nullptr);
    EXPECT_EQ(joinStatsBeforeJoin->pendingJoinGroupCount, 1u);
    EXPECT_EQ(joinStatsBeforeJoin->joinOverflowDropCount, 1u);

    sendTagged(rightSource, 200, 2);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({220}));

    EXPECT_EQ(pipeline->Stop(), ErrorCode::SUCCESS);
    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, PipelineObserver_AggregatesEdgeDropsToActors) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 1;
    config.idleWaitUs = 10;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = PipelineBuilder().AddModule(source).AddModule(sink).Connect("Source", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), ErrorCode::SUCCESS);

    source->Send(1, false);
    source->Send(2, false);
    source->Send(3, false);

    PipelineObserver observer(*pipeline);
    const auto observation = observer.Snapshot();
    ASSERT_EQ(observation.ports.size(), 1u);
    ASSERT_EQ(observation.actors.size(), 2u);

    const auto* sourceStats = FindActorStats(observation, "Source");
    const auto* sinkStats = FindActorStats(observation, "Sink");
    ASSERT_NE(sourceStats, nullptr);
    ASSERT_NE(sinkStats, nullptr);

    EXPECT_EQ(sourceStats->outgoingEnqueueCount, 1u);
    EXPECT_EQ(sourceStats->outgoingDropCount, 2u);
    EXPECT_EQ(sourceStats->outgoingRejectCount, 0u);
    EXPECT_EQ(sinkStats->incomingDequeueCount, 0u);

    EXPECT_EQ(pipeline->DeInit(), ErrorCode::SUCCESS);
}

TEST(PipelineRuntimeTest, PortRuntimeStatsState_CurrentDepthDoesNotLeakWhenDequeueWinsRace) {
    executor::Executor::PortRuntimeStatsState stats("Source", "out", "Sink", "in");

    stats.RecordDequeue();
    stats.RecordPushAccepted(1, 0);

    const auto snapshot = stats.Snapshot();
    EXPECT_EQ(snapshot.enqueueCount, 1u);
    EXPECT_EQ(snapshot.dequeueCount, 1u);
    EXPECT_EQ(snapshot.currentDepth, 0u);
}
