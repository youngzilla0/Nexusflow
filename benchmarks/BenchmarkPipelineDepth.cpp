#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

static void BM_ReportPipelineLinearDepth_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr int kTestCount = 20000;

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildLinearPipeline(source, sink, depth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = std::chrono::steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, kTestCount,
                                                     std::chrono::seconds(5));
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, std::chrono::seconds(1));
        const auto endTime = std::chrono::steady_clock::now();

        const auto elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(),
                              static_cast<std::uint64_t>(elapsedNs),
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineLinearDepth_Blocking)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(32)->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineLinearDepthPayload1KiB_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr int kTestCount = 20000;
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<PayloadSourceModule>("Source", kPayloadSize, true);
    auto sink = std::make_shared<CountingSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildPayloadPipeline(source, sink, depth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = std::chrono::steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, kTestCount,
                                                     std::chrono::seconds(5));
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, std::chrono::seconds(1));
        const auto endTime = std::chrono::steady_clock::now();

        const auto elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), 0,
                              static_cast<std::uint64_t>(elapsedNs),
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
        state.counters["EffectivePayloadMiBps"] =
            elapsedNs == 0 ? 0.0 : (static_cast<double>(sink->GetMessageCount()) * kPayloadSize * 1e9) /
                                      (static_cast<double>(elapsedNs) * 1024.0 * 1024.0);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineLinearDepthPayload1KiB_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Unit(benchmark::kMicrosecond);
