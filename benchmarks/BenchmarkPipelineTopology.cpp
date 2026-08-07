#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

namespace {

constexpr int kTopologyCompareCount = 10000;
constexpr int kTopologyLatencySampleCount = 2000;
constexpr std::size_t kTopologyQueueSize = 10000;
constexpr std::size_t kTopologyWorkers = 8;
constexpr std::size_t kTopologyMaxPendingGroups = 20000;

enum class TopologyKind : int {
    Linear = 1,
    DiamondJoin = 2,
};

PipelineConfig MakeTopologyConfig() {
    PipelineConfig config;
    config.queueSize = kTopologyQueueSize;
    config.idleWaitUs = 5;
    config.executorThreadCount = kTopologyWorkers;
    config.maxPendingJoinGroups = kTopologyMaxPendingGroups;
    return config;
}

void RunTopologyIteration(benchmark::State& state,
                          Pipeline& pipeline,
                          const std::function<void()>& resetFn,
                          const std::function<std::uint64_t()>& sourceCountFn,
                          const std::function<std::uint64_t()>& sinkCountFn,
                          const std::function<std::uint64_t()>& latencyTotalFn,
                          const std::function<void()>& emitFn,
                          std::uint64_t expectedCount) {
    resetFn();
    const auto portStatsBefore = SummarizePortStats(pipeline);

    const auto startTime = std::chrono::steady_clock::now();
    for (int i = 0; i < kTopologyCompareCount; ++i) {
        emitFn();
    }
    const bool delivered =
        WaitForCounterAtLeast([&sinkCountFn]() { return sinkCountFn(); }, expectedCount, std::chrono::seconds(10));
    const bool drained = delivered && WaitForPipelineDrain(pipeline, std::chrono::seconds(1));
    const auto endTime = std::chrono::steady_clock::now();

    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    const auto portStatsAfter = SummarizePortStats(pipeline);
    const auto nodeStats = SummarizeNodeStats(pipeline);

    PublishReportCounters(state, sourceCountFn(), sinkCountFn(), latencyTotalFn(),
                          static_cast<std::uint64_t>(elapsedNs), DiffPortStats(portStatsBefore, portStatsAfter),
                          delivered, drained);
    PublishNodeCounters(state, nodeStats);
}

template <typename LatenciesFn>
void RunTopologyLatencyIteration(benchmark::State& state,
                                 Pipeline& pipeline,
                                 const std::function<void()>& resetFn,
                                 const std::function<std::uint64_t()>& sourceCountFn,
                                 const std::function<std::uint64_t()>& sinkCountFn,
                                 const std::function<std::uint64_t()>& latencyTotalFn,
                                 LatenciesFn&& latenciesFn,
                                 const std::function<void()>& emitFn,
                                 std::uint64_t expectedCount) {
    resetFn();
    const auto portStatsBefore = SummarizePortStats(pipeline);

    const auto startTime = std::chrono::steady_clock::now();
    for (std::uint64_t i = 0; i < expectedCount; ++i) {
        emitFn();
        if (!WaitForCounterAtLeast([&sinkCountFn]() { return sinkCountFn(); }, i + 1, std::chrono::milliseconds(100))) {
            break;
        }
    }
    const bool delivered =
        WaitForCounterAtLeast([&sinkCountFn]() { return sinkCountFn(); }, expectedCount, std::chrono::seconds(2));
    const bool drained = delivered && WaitForPipelineDrain(pipeline, std::chrono::seconds(1));
    const auto endTime = std::chrono::steady_clock::now();

    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    const auto portStatsAfter = SummarizePortStats(pipeline);
    const auto nodeStats = SummarizeNodeStats(pipeline);

    PublishReportCounters(state, sourceCountFn(), sinkCountFn(), latencyTotalFn(),
                          static_cast<std::uint64_t>(elapsedNs), DiffPortStats(portStatsBefore, portStatsAfter),
                          delivered, drained);
    PublishLatencyCounters(state, latenciesFn());
    PublishNodeCounters(state, nodeStats);
}

void PublishTopologyShapeCounters(benchmark::State& state, TopologyKind topologyKind, int depth, int branches) {
    state.counters["TopologyKind"] = static_cast<double>(static_cast<int>(topologyKind));
    state.counters["Depth"] = static_cast<double>(depth);
    state.counters["Branches"] = static_cast<double>(branches);
}

} // namespace

static void BM_ReportPipelineTopologyCompare_LinearTimestamp_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    auto pipeline = BuildLinearPipeline(source, sink, depth, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyIteration(
            state, *pipeline,
            [&]() {
                sink->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return sink->GetMessageCount(); },
            [&]() { return sink->GetTotalLatencyNs(); },
            [&]() { source->GenerateOne(); },
            kTopologyCompareCount);
        PublishTopologyShapeCounters(state, TopologyKind::Linear, depth, 1);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_LinearTimestamp_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_DiamondTimestamp_BlockingJoin(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));

    auto source = std::make_shared<SourceModule>("Source", true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<JoinSinkModule>("Join", joinPorts);

    auto pipeline = BuildDiamondJoinPipeline(source, join, branches, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyIteration(
            state, *pipeline,
            [&]() {
                join->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return join->GetMessageCount(); },
            [&]() { return join->GetTotalLatencyNs(); },
            [&]() { source->GenerateOne(); },
            kTopologyCompareCount);
        PublishTopologyShapeCounters(state, TopologyKind::DiamondJoin, 1, branches);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_DiamondTimestamp_BlockingJoin)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_LinearPayload1KiB_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<PayloadSourceModule>("Source", kPayloadSize, true);
    auto sink = std::make_shared<CountingSinkModule>("Sink");

    auto pipeline = BuildPayloadPipeline(source, sink, depth, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyIteration(
            state, *pipeline,
            [&]() {
                sink->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return sink->GetMessageCount(); },
            []() { return 0ULL; },
            [&]() { source->GenerateOne(); },
            kTopologyCompareCount);
        PublishTopologyShapeCounters(state, TopologyKind::Linear, depth, 1);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
        const auto elapsedUs = state.counters["ElapsedUs"];
        const double seconds = static_cast<double>(elapsedUs) / 1e6;
        state.counters["EffectivePayloadMiBps"] =
            seconds <= 0.0 ? 0.0
                           : (static_cast<double>(sink->GetMessageCount()) * static_cast<double>(kPayloadSize)) /
                                 (seconds * 1024.0 * 1024.0);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_LinearPayload1KiB_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_DiamondPayload1KiB_BlockingJoin(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<PayloadSourceModule>("Source", kPayloadSize, true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<CountingJoinSinkModule>("Join", joinPorts);

    auto pipeline = BuildDiamondJoinPayloadPipeline(source, join, branches, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyIteration(
            state, *pipeline,
            [&]() {
                join->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return join->GetMessageCount(); },
            []() { return 0ULL; },
            [&]() { source->GenerateOne(); },
            kTopologyCompareCount);
        PublishTopologyShapeCounters(state, TopologyKind::DiamondJoin, 1, branches);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
        const auto elapsedUs = state.counters["ElapsedUs"];
        const double seconds = static_cast<double>(elapsedUs) / 1e6;
        state.counters["EffectivePayloadMiBps"] =
            seconds <= 0.0 ? 0.0
                           : (static_cast<double>(join->GetMessageCount()) * static_cast<double>(kPayloadSize)) /
                                 (seconds * 1024.0 * 1024.0);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_DiamondPayload1KiB_BlockingJoin)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_LinearTimestampLatency_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    auto pipeline = BuildLinearPipeline(source, sink, depth, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyLatencyIteration(
            state, *pipeline,
            [&]() {
                sink->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return sink->GetMessageCount(); },
            [&]() { return sink->GetTotalLatencyNs(); },
            [&]() { return sink->LatenciesSnapshot(); },
            [&]() { source->GenerateOne(); },
            kTopologyLatencySampleCount);
        PublishTopologyShapeCounters(state, TopologyKind::Linear, depth, 1);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_LinearTimestampLatency_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_DiamondTimestampLatency_BlockingJoin(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));

    auto source = std::make_shared<SourceModule>("Source", true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<JoinLatencySinkModule>("Join", joinPorts);

    auto pipeline = BuildDiamondJoinLatencyPipeline(source, join, branches, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyLatencyIteration(
            state, *pipeline,
            [&]() {
                join->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return join->GetMessageCount(); },
            [&]() { return join->GetTotalLatencyNs(); },
            [&]() { return join->LatenciesSnapshot(); },
            [&]() { source->GenerateOne(); },
            kTopologyLatencySampleCount);
        PublishTopologyShapeCounters(state, TopologyKind::DiamondJoin, 1, branches);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_DiamondTimestampLatency_BlockingJoin)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_LinearPayload1KiBLatency_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<TimedPayloadSourceModule>("Source", kPayloadSize, true);
    auto sink = std::make_shared<TimedPayloadLatencySinkModule>("Sink");

    auto pipeline = BuildTimedPayloadPipeline(source, sink, depth, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyLatencyIteration(
            state, *pipeline,
            [&]() {
                sink->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return sink->GetMessageCount(); },
            [&]() { return sink->GetTotalLatencyNs(); },
            [&]() { return sink->LatenciesSnapshot(); },
            [&]() { source->GenerateOne(); },
            kTopologyLatencySampleCount);
        PublishTopologyShapeCounters(state, TopologyKind::Linear, depth, 1);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_LinearPayload1KiBLatency_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineTopologyCompare_DiamondPayload1KiBLatency_BlockingJoin(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<TimedPayloadSourceModule>("Source", kPayloadSize, true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<TimedPayloadJoinLatencySinkModule>("Join", joinPorts);

    auto pipeline = BuildDiamondJoinTimedPayloadLatencyPipeline(source, join, branches, MakeTopologyConfig(), true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunTopologyLatencyIteration(
            state, *pipeline,
            [&]() {
                join->Reset();
                source->ResetCounter();
            },
            [&]() { return source->GetCounter(); },
            [&]() { return join->GetMessageCount(); },
            [&]() { return join->GetTotalLatencyNs(); },
            [&]() { return join->LatenciesSnapshot(); },
            [&]() { source->GenerateOne(); },
            kTopologyLatencySampleCount);
        PublishTopologyShapeCounters(state, TopologyKind::DiamondJoin, 1, branches);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineTopologyCompare_DiamondPayload1KiBLatency_BlockingJoin)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Unit(benchmark::kMicrosecond);
