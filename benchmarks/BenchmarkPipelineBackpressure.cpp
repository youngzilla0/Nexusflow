#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

static void BM_ReportPipelineQueueCapacity_NonBlocking(benchmark::State& state) {
    const auto queueSize = static_cast<std::size_t>(state.range(0));
    constexpr int kTestCount = 50000;
    constexpr int kDepth = 4;

    auto source = std::make_shared<SourceModule>("Source", false);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = queueSize;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = BuildLinearPipeline(source, sink, kDepth, config, false, 10);
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
        const bool drained = WaitForPipelineDrain(*pipeline, std::chrono::seconds(5));
        const auto endTime = std::chrono::steady_clock::now();

        const auto elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(),
                              static_cast<std::uint64_t>(elapsedNs),
                              DiffPortStats(portStatsBefore, portStatsAfter), true, drained);
        state.counters["QueueSize"] = static_cast<double>(queueSize);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineQueueCapacity_NonBlocking)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Unit(benchmark::kMicrosecond);
