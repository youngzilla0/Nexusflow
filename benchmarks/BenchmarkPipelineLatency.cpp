#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

namespace {

template <typename SourcePtr, typename SinkPtr>
void RunLatencyIteration(benchmark::State& state,
                         const std::unique_ptr<Pipeline>& pipeline,
                         const SourcePtr& source,
                         const SinkPtr& sink,
                         int sampleCount,
                         const PortStatsSummary& portStatsBefore) {
    const auto startTime = std::chrono::steady_clock::now();
    for (int i = 0; i < sampleCount; ++i) {
        source->GenerateOne();
        const auto expectedCount = static_cast<std::uint64_t>(i + 1);
        if (!WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, expectedCount,
                                   std::chrono::milliseconds(100))) {
            break;
        }
    }
    const bool delivered =
        WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, static_cast<std::uint64_t>(sampleCount),
                              std::chrono::seconds(1));
    const bool drained = delivered && WaitForPipelineDrain(*pipeline, std::chrono::milliseconds(500));
    const auto endTime = std::chrono::steady_clock::now();

    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    const auto portStatsAfter = SummarizePortStats(*pipeline);
    const auto latencies = sink->LatenciesSnapshot();

    PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(),
                          static_cast<std::uint64_t>(elapsedNs),
                          DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    PublishLatencyCounters(state, latencies);
}

} // namespace

static void BM_ReportPipelineLinearDepthLatency_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr int kSampleCount = 2000;

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 16;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildLinearPipeline(source, sink, depth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);
        RunLatencyIteration(state, pipeline, source, sink, kSampleCount, portStatsBefore);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineLinearDepthLatency_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineLinearDepthPayload1KiBLatency_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr int kSampleCount = 2000;
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<TimedPayloadSourceModule>("Source", kPayloadSize, true);
    auto sink = std::make_shared<TimedPayloadLatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 16;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildTimedPayloadPipeline(source, sink, depth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);
        RunLatencyIteration(state, pipeline, source, sink, kSampleCount, portStatsBefore);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineLinearDepthPayload1KiBLatency_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineDiamondJoinLatency_Blocking(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));
    constexpr int kSampleCount = 2000;

    auto source = std::make_shared<SourceModule>("Source", true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<JoinLatencySinkModule>("Join", joinPorts);

    PipelineConfig config;
    config.queueSize = 16;
    config.idleWaitUs = 5;
    config.executorThreadCount = 8;
    config.maxPendingJoinGroups = 4096;

    auto pipeline = BuildDiamondJoinLatencyPipeline(source, join, branches, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        join->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        RunLatencyIteration(state, pipeline, source, join, kSampleCount, portStatsBefore);
        state.counters["BranchCount"] = static_cast<double>(branches);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineDiamondJoinLatency_Blocking)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMicrosecond);
