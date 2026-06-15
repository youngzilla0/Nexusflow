#include <gtest/gtest.h>

#include "executor/Executor.hpp"
#include "base/Graph.hpp"

#include <nexusflow/Nexusflow.hpp>

#include <chrono>
#include <fstream>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

using namespace nexusflow;
using namespace std::chrono_literals;

namespace {

struct LifecycleRecorder {
    void Record(const std::string& event) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back(event);
    }

    std::vector<std::string> Snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return events;
    }

    mutable std::mutex mutex;
    std::vector<std::string> events;
};

bool g_yamlStatisticsEnabled = true;
std::size_t g_yamlExecutorThreadCount = 0;
std::size_t g_yamlQueueSize = 0;

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

    Error Init() override {
        m_statisticsEnabled = GetPipelineContext().IsStatisticsEnabled();
        m_throughputEnabled = GetPipelineContext().IsThroughputStatisticsEnabled();
        m_latencyEnabled = GetPipelineContext().IsLatencyStatisticsEnabled();
        return Error::Ok();
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

class LifecycleProbeModule : public Module {
public:
    LifecycleProbeModule(std::string name, std::shared_ptr<LifecycleRecorder> recorder)
        : Module(std::move(name)), m_recorder(std::move(recorder)) {}

    Error Init() override {
        m_recorder->Record("Init:" + GetModuleName());
        return Error::Ok();
    }

    Error DeInit() override {
        m_recorder->Record("DeInit:" + GetModuleName());
        return Error::Ok();
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        auto* message = inputs.OnlyMessage();
        if (message != nullptr) {
            outputs.Emit(*message, true);
        }
    }

private:
    std::shared_ptr<LifecycleRecorder> m_recorder;
};

class FailingInitModule : public Module {
public:
    explicit FailingInitModule(std::string name) : Module(std::move(name)) {}

    Error Init() override { return Error::Err(Error::Code::Failure, "Module initialization failed."); }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }
};

class YamlSourceModule : public Module {
public:
    explicit YamlSourceModule(std::string name) : Module(std::move(name)) { SetSourcePolicy(SourcePolicy::Manual); }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }
};

class YamlSinkModule : public Module {
public:
    explicit YamlSinkModule(std::string name) : Module(std::move(name)) {}

    Error Init() override {
        g_yamlStatisticsEnabled = GetPipelineContext().IsStatisticsEnabled();
        g_yamlExecutorThreadCount = GetPipelineContext().GetExecutorThreadCount();
        g_yamlQueueSize = GetPipelineContext().GetConfig().queueSize;
        return Error::Ok();
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }
};

PortStats GetOnlyPortStats(const Pipeline& pipeline) {
    auto stats = pipeline.GetPortStats();
    EXPECT_EQ(stats.size(), 1u);
    return stats.empty() ? PortStats{} : stats.front();
}

const NodeStats* FindNodeStats(const PipelineObservation& observation, const std::string& nodeName) {
    for (const auto& node : observation.nodes) {
        if (node.nodeName == nodeName) {
            return &node;
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
    ASSERT_TRUE(pipeline->Init().IsOk());

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

    ASSERT_TRUE(pipeline->Start().IsOk());
    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({1}));

    auto afterStart = GetOnlyPortStats(*pipeline);
    EXPECT_EQ(afterStart.dequeueCount, 1u);
    EXPECT_EQ(afterStart.currentDepth, 0u);

    EXPECT_TRUE(pipeline->Stop().IsOk());
    EXPECT_TRUE(pipeline->DeInit().IsOk());
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
    ASSERT_TRUE(pipeline->Init().IsOk());

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

    ASSERT_TRUE(pipeline->Start().IsOk());
    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({3}));

    auto afterStart = GetOnlyPortStats(*pipeline);
    EXPECT_EQ(afterStart.dequeueCount, 1u);
    EXPECT_EQ(afterStart.currentDepth, 0u);

    EXPECT_TRUE(pipeline->Stop().IsOk());
    EXPECT_TRUE(pipeline->DeInit().IsOk());
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
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    ASSERT_EQ(pipeline->Start(), Error::Ok());

    source->Send(42, true);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({42}));

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
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
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    ASSERT_EQ(pipeline->Start(), Error::Ok());

    std::this_thread::sleep_for(20ms);
    source->Send(42, true);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({42}));

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, SingleWorkerPipeline_CompletesWithoutTimeoutOnShortChain) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto pass = std::make_shared<ForwardModule>("Pass");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.executorThreadCount = 1;
    config.queueSize = 8;
    config.idleWaitUs = 0;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass)
                        .AddModule(sink)
                        .Connect("Source", "Pass")
                        .Connect("Pass", "Sink")
                        .WithConfig(config)
                        .Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    ASSERT_EQ(pipeline->Start(), Error::Ok());

    source->Send(7, true);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({7}));

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, PipelineExposesTopologyForkJoinGroups) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto left = std::make_shared<ForwardModule>("Left");
    auto right = std::make_shared<ForwardModule>("Right");
    auto join = std::make_shared<ForwardModule>("Join");

    PipelineConfig config;
    config.executorThreadCount = 1;
    config.queueSize = 8;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(left)
                        .AddModule(right)
                        .AddModule(join)
                        .Connect("Source", "Left")
                        .Connect("Source", "Right")
                        .Connect("Left", "Join")
                        .Connect("Right", "Join")
                        .WithConfig(config)
                        .Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());

    auto topology = pipeline->GetTopologyInfo();
    ASSERT_EQ(topology.forkJoinGroups.size(), 1u);
    EXPECT_EQ(topology.forkJoinGroups[0].forkNode->name, "Source");
    EXPECT_EQ(topology.forkJoinGroups[0].joinNode->name, "Join");
    EXPECT_EQ(topology.forkJoinGroups[0].paths.size(), 2u);

    EXPECT_EQ(pipeline->Start(), Error::Ok());
    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, Lifecycle_OrderFollowsTopologyAndReverseTopology) {
    auto recorder = std::make_shared<LifecycleRecorder>();
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto pass = std::make_shared<LifecycleProbeModule>("Pass", recorder);
    auto sink = std::make_shared<LifecycleProbeModule>("Sink", recorder);

    PipelineConfig config;
    config.executorThreadCount = 1;
    config.queueSize = 8;

    auto pipeline =
        PipelineBuilder().AddModule(source).AddModule(pass).AddModule(sink).Connect("Source", "Pass").Connect("Pass", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    EXPECT_EQ(recorder->Snapshot(), std::vector<std::string>({"Init:Pass", "Init:Sink"}));

    ASSERT_EQ(pipeline->Start(), Error::Ok());
    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
    EXPECT_EQ(recorder->Snapshot(),
              std::vector<std::string>({"Init:Pass", "Init:Sink", "DeInit:Sink", "DeInit:Pass"}));
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
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    EXPECT_FALSE(pass->StatisticsEnabled());
    EXPECT_FALSE(pass->ThroughputEnabled());
    EXPECT_FALSE(pass->LatencyEnabled());

    source->Send(7, true);

    ASSERT_EQ(pipeline->Start(), Error::Ok());
    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({7}));
    EXPECT_TRUE(pipeline->GetPortStats().empty());
    EXPECT_TRUE(pipeline->GetNodeStats().empty());

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
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
    ASSERT_EQ(pipeline->Init(), Error::Ok());

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

    ASSERT_EQ(pipeline->Start(), Error::Ok());
    std::this_thread::sleep_for(20ms);

    PipelineObserver observerBeforeJoin(*pipeline);
    const auto observationBeforeJoin = observerBeforeJoin.Snapshot();
    const auto* joinStatsBeforeJoin = FindNodeStats(observationBeforeJoin, "Join");
    ASSERT_NE(joinStatsBeforeJoin, nullptr);
    EXPECT_EQ(joinStatsBeforeJoin->pendingJoinGroupCount, 1u);
    EXPECT_EQ(joinStatsBeforeJoin->joinOverflowDropCount, 1u);

    sendTagged(rightSource, 200, 2);

    EXPECT_TRUE(sink->WaitForCount(1, 500ms));
    EXPECT_EQ(sink->Values(), std::vector<int>({220}));

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, OnAllInputs_CanCorrelateByTimestamp) {
    auto leftSource = std::make_shared<ManualSourceModule>("Left");
    auto rightSource = std::make_shared<ManualSourceModule>("Right");
    auto join = std::make_shared<JoinSumModule>("Join");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 8;
    config.idleWaitUs = 10;
    config.joinKeyPolicy = JoinKeyPolicy::Timestamp;
    config.fusionTimeoutMs = 60000;

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
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    ASSERT_EQ(pipeline->Start(), Error::Ok());

    const auto baseTimestampMs =
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count());

    auto sendTagged = [&](const std::shared_ptr<ManualSourceModule>& source, int value, std::uint64_t messageId,
                          std::uint64_t timestampOffsetMs) {
        auto message = MakeMessage(value, source->GetModuleName());
        message.MetaData().messageId = messageId;
        message.MetaData().timestamp = baseTimestampMs + timestampOffsetMs;
        source->SendMessage(std::move(message), true);
    };

    sendTagged(leftSource, 10, 1, 1000);
    std::this_thread::sleep_for(20ms);
    sendTagged(rightSource, 20, 2, 1000);

    if (!sink->WaitForCount(1, 500ms)) {
        PipelineObserver observer(*pipeline);
        ADD_FAILURE() << observer.Describe();
    }
    EXPECT_EQ(sink->Values(), std::vector<int>({30}));

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
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
    ASSERT_EQ(pipeline->Init(), Error::Ok());

    source->Send(1, false);
    source->Send(2, false);
    source->Send(3, false);

    PipelineObserver observer(*pipeline);
    const auto observation = observer.Snapshot();
    ASSERT_EQ(observation.ports.size(), 1u);
    ASSERT_EQ(observation.nodes.size(), 2u);

    const auto* sourceStats = FindNodeStats(observation, "Source");
    const auto* sinkStats = FindNodeStats(observation, "Sink");
    ASSERT_NE(sourceStats, nullptr);
    ASSERT_NE(sinkStats, nullptr);

    EXPECT_EQ(sourceStats->outgoingEnqueueCount, 1u);
    EXPECT_EQ(sourceStats->outgoingDropCount, 2u);
    EXPECT_EQ(sourceStats->outgoingRejectCount, 0u);
    EXPECT_EQ(sinkStats->incomingDequeueCount, 0u);

    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, PipelineObserverSummary_ComputesDropRateFromPorts) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 1;
    config.idleWaitUs = 10;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = PipelineBuilder().AddModule(source).AddModule(sink).Connect("Source", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());

    source->Send(1, false);
    source->Send(2, false);
    source->Send(3, false);

    PipelineObserver observer(*pipeline);
    const auto observation = observer.Snapshot();

    EXPECT_EQ(observation.summary.totalPushAttempts, 3u);
    EXPECT_EQ(observation.summary.totalEnqueueCount, 1u);
    EXPECT_EQ(observation.summary.totalDropCount, 2u);
    EXPECT_EQ(observation.summary.totalRejectCount, 0u);
    EXPECT_DOUBLE_EQ(observation.summary.dropRate, 2.0 / 3.0);
    EXPECT_DOUBLE_EQ(observation.summary.rejectRate, 0.0);

    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, PipelineObserverSummary_CollectsSinkLatencySamples) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto pass = std::make_shared<ForwardModule>("Pass");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.executorThreadCount = 2;
    config.queueSize = 8;
    config.idleWaitUs = 10;

    auto pipeline =
        PipelineBuilder().AddModule(source).AddModule(pass).AddModule(sink).Connect("Source", "Pass").Connect("Pass", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());
    ASSERT_EQ(pipeline->Start(), Error::Ok());

    auto message = MakeMessage(7, source->GetModuleName());
    const auto nowMs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    message.MetaData().timestamp = nowMs > 20 ? nowMs - 20 : 0;
    source->SendMessage(std::move(message), true);

    ASSERT_TRUE(sink->WaitForCount(1, 500ms));

    PipelineObserver observer(*pipeline);
    const auto observation = observer.Snapshot();
    EXPECT_GE(observation.summary.sinkReceiveCount, 1u);
    EXPECT_GE(observation.summary.latencySampleCount, 1u);
    EXPECT_GE(observation.summary.latencyP50Ms, 1u);
    EXPECT_GE(observation.summary.latencyP99Ms, observation.summary.latencyP50Ms);
    EXPECT_GE(observation.summary.latencyMaxMs, observation.summary.latencyP99Ms);

    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, PipelineObserverEvents_LifecycleCallbacksFireInOrder) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    auto pipeline =
        PipelineBuilder().WithName("LifecyclePipeline").AddModule(source).AddModule(sink).Connect("Source", "Sink").Build();

    ASSERT_NE(pipeline, nullptr);

    auto observer = std::make_shared<CallbackPipelineObserver>();
    std::mutex mutex;
    std::vector<std::string> events;
    observer->OnInitialized([&](const std::string& pipelineName) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("init:" + pipelineName);
    });
    observer->OnStarted([&](const std::string& pipelineName) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("start:" + pipelineName);
    });
    observer->OnStopped([&](const std::string& pipelineName) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("stop:" + pipelineName);
    });
    observer->OnDeInitialized([&](const std::string& pipelineName) {
        std::lock_guard<std::mutex> lock(mutex);
        events.push_back("deinit:" + pipelineName);
    });

    pipeline->AddObserver(observer);

    EXPECT_EQ(pipeline->Init(), Error::Ok());
    EXPECT_EQ(pipeline->Start(), Error::Ok());
    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());

    const std::vector<std::string> expected = {
        "init:LifecyclePipeline",
        "start:LifecyclePipeline",
        "stop:LifecyclePipeline",
        "deinit:LifecyclePipeline",
    };

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_EQ(events, expected);
}

TEST(PipelineRuntimeTest, PipelineObserverEvents_DropEventIsDelivered) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 1;
    config.idleWaitUs = 10;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = PipelineBuilder().WithName("DropEventPipeline").AddModule(source).AddModule(sink).Connect("Source", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());

    auto observer = std::make_shared<CallbackPipelineObserver>();
    std::mutex mutex;
    std::vector<PipelineMessageEvent> messageEvents;
    observer->OnMessage([&](const PipelineMessageEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        messageEvents.push_back(event);
    });
    pipeline->AddObserver(observer);

    source->Send(1, false);
    source->Send(2, false);

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(messageEvents.size(), 1u);
    EXPECT_EQ(messageEvents[0].pipelineName, "DropEventPipeline");
    EXPECT_EQ(messageEvents[0].type, PipelineMessageEventType::Dropped);
    EXPECT_EQ(messageEvents[0].srcNodeName, "Source");
    EXPECT_EQ(messageEvents[0].srcPortName, kDefaultOutputPort);
    EXPECT_EQ(messageEvents[0].dstNodeName, "Sink");
    EXPECT_EQ(messageEvents[0].dstInputPortName, kDefaultInputPort);
    EXPECT_EQ(messageEvents[0].affectedCount, 1u);
    EXPECT_FALSE(messageEvents[0].blocking);
    EXPECT_EQ(messageEvents[0].reason, "drop tail overflow");

    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, PipelineObserverEvents_DropHeadEventReportsEvictedMessage) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 1;
    config.idleWaitUs = 10;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropHead;

    auto pipeline =
        PipelineBuilder().WithName("DropHeadEventPipeline").AddModule(source).AddModule(sink).Connect("Source", "Sink").WithConfig(config).Build();

    ASSERT_NE(pipeline, nullptr);
    ASSERT_EQ(pipeline->Init(), Error::Ok());

    auto observer = std::make_shared<CallbackPipelineObserver>();
    std::mutex mutex;
    std::vector<PipelineMessageEvent> messageEvents;
    observer->OnMessage([&](const PipelineMessageEvent& event) {
        std::lock_guard<std::mutex> lock(mutex);
        messageEvents.push_back(event);
    });
    pipeline->AddObserver(observer);

    source->Send(1, false);
    source->Send(2, false);

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(messageEvents.size(), 1u);
    EXPECT_EQ(messageEvents[0].pipelineName, "DropHeadEventPipeline");
    EXPECT_EQ(messageEvents[0].type, PipelineMessageEventType::Dropped);
    EXPECT_EQ(messageEvents[0].srcNodeName, "Source");
    EXPECT_EQ(messageEvents[0].srcPortName, kDefaultOutputPort);
    EXPECT_EQ(messageEvents[0].dstNodeName, "Sink");
    EXPECT_EQ(messageEvents[0].dstInputPortName, kDefaultInputPort);
    EXPECT_EQ(messageEvents[0].affectedCount, 1u);
    EXPECT_FALSE(messageEvents[0].blocking);
    EXPECT_EQ(messageEvents[0].reason, "drop head overflow");

    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, PortStatsState_CurrentDepthDoesNotLeakWhenDequeueWinsRace) {
    executor::Executor::PortStatsState stats("Source", "out", "Sink", "in");

    stats.RecordDequeue();
    stats.RecordPushAccepted(1, 0);

    const auto snapshot = stats.Snapshot();
    EXPECT_EQ(snapshot.enqueueCount, 1u);
    EXPECT_EQ(snapshot.dequeueCount, 1u);
    EXPECT_EQ(snapshot.currentDepth, 0u);
}

TEST(PipelineRuntimeTest, PipelineBuilder_RejectsDuplicateModuleNames) {
    auto source1 = std::make_shared<ManualSourceModule>("Duplicated");
    auto source2 = std::make_shared<ManualSourceModule>("Duplicated");

    auto pipeline = PipelineBuilder().WithName("DuplicateNames").AddModule(source1).AddModule(source2).Build();
    EXPECT_EQ(pipeline, nullptr);
}

TEST(PipelineRuntimeTest, PipelineBuilder_RejectsConnectionsToMissingModules) {
    auto source = std::make_shared<ManualSourceModule>("Source");

    auto pipeline = PipelineBuilder().WithName("MissingNode").AddModule(source).Connect("Source", "Sink").Build();
    EXPECT_EQ(pipeline, nullptr);
}

TEST(PipelineRuntimeTest, PipelineBuilder_RejectsMultipleModulesWithoutConnections) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    auto pipeline = PipelineBuilder().WithName("DisconnectedGraph").AddModule(source).AddModule(sink).Build();
    EXPECT_EQ(pipeline, nullptr);
}

TEST(PipelineRuntimeTest, PipelineBuilder_OnErrorCallbackReceivesInitFailure) {
    auto source = std::make_shared<ManualSourceModule>("Source");
    auto fail = std::make_shared<FailingInitModule>("Fail");
    auto sink = std::make_shared<CollectSinkModule>("Sink");

    std::mutex mutex;
    std::vector<PipelineErrorEvent> errors;

    auto pipeline = PipelineBuilder()
                        .WithName("ErrorCallbackPipeline")
                        .AddModule(source)
                        .AddModule(fail)
                        .AddModule(sink)
                        .Connect("Source", "Fail")
                        .Connect("Fail", "Sink")
                        .OnError([&](const PipelineErrorEvent& event) {
                            std::lock_guard<std::mutex> lock(mutex);
                            errors.push_back(event);
                        })
                        .Build();

    ASSERT_NE(pipeline, nullptr);
    auto initResult = pipeline->Init();
    EXPECT_TRUE(initResult.IsErr());
    EXPECT_EQ(initResult.GetCode(), Error::Code::Failure);

    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0].pipelineName, "ErrorCallbackPipeline");
    EXPECT_EQ(errors[0].stage, "Init");
    EXPECT_EQ(errors[0].nodeName, "Fail");
    EXPECT_EQ(errors[0].code, Error::Code::Failure);
    EXPECT_EQ(errors[0].message, "Module initialization failed.");

    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}

TEST(PipelineRuntimeTest, CreateFromYaml_LoadsRuntimeConfig) {
    NEXUSFLOW_REGISTER_MODULE(YamlSourceModule);
    NEXUSFLOW_REGISTER_MODULE(YamlSinkModule);

    const std::string yamlPath = "/tmp/nexusflow_runtime_config_test.yaml";
    std::ofstream out(yamlPath);
    out << "runtime:\n";
    out << "  executorThreadCount: 3\n";
    out << "  queueSize: 9\n";
    out << "  idleWaitUs: 7\n";
    out << "  joinKeyPolicy: Timestamp\n";
    out << "  nonBlockingQueueFullPolicy: DropHead\n";
    out << "  statistics:\n";
    out << "    enableStatistics: false\n";
    out << "    enableThroughput: false\n";
    out << "    enableLatency: false\n";
    out << "graph:\n";
    out << "  name: RuntimeConfigGraph\n";
    out << "  modules:\n";
    out << "    - name: Source\n";
    out << "      class: YamlSourceModule\n";
    out << "    - name: Sink\n";
    out << "      class: YamlSinkModule\n";
    out << "  connections:\n";
    out << "    - from: Source\n";
    out << "      to: Sink\n";
    out.close();

    auto pipeline = Pipeline::CreateFromYaml(yamlPath);
    ASSERT_NE(pipeline, nullptr);

    ASSERT_EQ(pipeline->Init(), Error::Ok());
    EXPECT_FALSE(g_yamlStatisticsEnabled);
    EXPECT_EQ(g_yamlExecutorThreadCount, 3u);
    EXPECT_EQ(g_yamlQueueSize, 9u);
    ASSERT_EQ(pipeline->Start(), Error::Ok());
    EXPECT_TRUE(pipeline->GetPortStats().empty());
    EXPECT_TRUE(pipeline->GetNodeStats().empty());
    EXPECT_EQ(pipeline->Stop(), Error::Ok());
    EXPECT_EQ(pipeline->DeInit(), Error::Ok());
}
