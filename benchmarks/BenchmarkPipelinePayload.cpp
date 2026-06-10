#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

static void BM_ReportPipelinePayloadSize_Blocking(benchmark::State& state) {
    const auto payloadSize = static_cast<std::size_t>(state.range(0));
    constexpr int kTestCount = 20000;
    constexpr int kDepth = 4;

    auto source = std::make_shared<PayloadSourceModule>("Source", payloadSize, true);
    auto sink = std::make_shared<CountingSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildPayloadPipeline(source, sink, kDepth, config, true);
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
        state.counters["PayloadBytes"] = static_cast<double>(payloadSize);
        state.counters["EffectivePayloadMiBps"] =
            elapsedNs == 0 ? 0.0 : (static_cast<double>(sink->GetMessageCount()) * payloadSize * 1e9) /
                                      (static_cast<double>(elapsedNs) * 1024.0 * 1024.0);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelinePayloadSize_Blocking)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Unit(benchmark::kMicrosecond);
