#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

static void BM_ReportPipelineWorkerScaling_Blocking(benchmark::State& state) {
    const auto workers = static_cast<std::size_t>(state.range(0));
    constexpr int kTestCount = 20000;
    constexpr int kDepth = 8;

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = workers;

    auto pipeline = BuildLinearPipeline(source, sink, kDepth, config, true);
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
BENCHMARK(BM_ReportPipelineWorkerScaling_Blocking)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineDiamondBranches_BlockingJoin(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));
    constexpr int kTestCount = 10000;

    auto source = std::make_shared<SourceModule>("Source", true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<JoinSinkModule>("Join", joinPorts);

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 8;
    config.maxPendingJoinGroups = 20000;

    auto pipeline = BuildDiamondJoinPipeline(source, join, branches, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        join->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = std::chrono::steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&join]() { return join->GetMessageCount(); }, kTestCount,
                                                     std::chrono::seconds(5));
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, std::chrono::seconds(1));
        const auto endTime = std::chrono::steady_clock::now();

        const auto elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), join->GetMessageCount(), join->GetTotalLatencyNs(),
                              static_cast<std::uint64_t>(elapsedNs),
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
        state.counters["BranchCount"] = static_cast<double>(branches);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineDiamondBranches_BlockingJoin)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMicrosecond);
