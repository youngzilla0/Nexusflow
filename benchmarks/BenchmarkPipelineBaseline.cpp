#include "PipelineBenchmarkCommon.hpp"

using namespace nexusflow;
using namespace nexusflow::bench;

namespace {

void RunFixedDurationIteration(benchmark::State& state, const std::unique_ptr<Pipeline>& pipeline,
                               const std::shared_ptr<SourceModule>& source,
                               const std::shared_ptr<LatencySinkModule>& sink,
                               std::chrono::seconds duration,
                               std::uint64_t expectedMultiplier,
                               std::chrono::seconds deliveryTimeout) {
    sink->Reset();
    source->ResetCounter();
    const auto portStatsBefore = SummarizePortStats(*pipeline);

    const auto startTime = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - startTime < duration) {
        source->GenerateOne();
    }
    const auto sourceSent = source->GetCounter();
    const bool delivered = WaitForCounterAtLeast(
        [&sink]() { return sink->GetMessageCount(); }, sourceSent * expectedMultiplier, deliveryTimeout);
    const bool drained = delivered && WaitForPipelineDrain(*pipeline, std::chrono::milliseconds(500));
    const auto endTime = std::chrono::steady_clock::now();

    const auto elapsedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
    const auto portStatsAfter = SummarizePortStats(*pipeline);

    PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(),
                            static_cast<std::uint64_t>(elapsedNs),
                            DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
}

} // namespace

static void BM_PipelineLinear_Throughput_Blocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = BuildLinearPipeline(source, sink, 2, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunFixedDurationIteration(state, pipeline, source, sink, std::chrono::seconds(1), 1, std::chrono::seconds(2));
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineLinear_Throughput_Blocking)->Unit(benchmark::kMicrosecond);

static void BM_PipelineSinglePath_Throughput_Blocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = BuildLinearPipeline(source, sink, 1, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        RunFixedDurationIteration(state, pipeline, source, sink, std::chrono::seconds(1), 1, std::chrono::seconds(2));
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineSinglePath_Throughput_Blocking)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_BlockingWarm(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    PipelineBuilder builder;
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    builder.AddModule(source)
        .AddModule(pass1)
        .AddModule(pass2)
        .AddModule(sink)
        .Connect("Source", "Pass1")
        .Connect("Source", "Pass2")
        .Connect("Pass1", "Sink")
        .Connect("Pass2", "Sink")
        .WithConfig(config);
    auto pipeline = builder.Build();
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();

        for (int i = 0; i < 100; ++i) {
            source->GenerateOne();
        }
        while (sink->GetMessageCount() < 200) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }

        RunFixedDurationIteration(state, pipeline, source, sink, std::chrono::seconds(1), 2, std::chrono::seconds(2));
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_BlockingWarm)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_NonBlocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", false);
    auto sink = std::make_shared<LatencySinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    PipelineBuilder builder;
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", false);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", false);
    builder.AddModule(source)
        .AddModule(pass1)
        .AddModule(pass2)
        .AddModule(sink)
        .Connect("Source", "Pass1")
        .Connect("Source", "Pass2")
        .Connect("Pass1", "Sink")
        .Connect("Pass2", "Sink")
        .WithConfig(config);
    auto pipeline = builder.Build();
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const bool drained = WaitForPipelineDrain(*pipeline, std::chrono::seconds(2));
        const auto endTime = std::chrono::steady_clock::now();

        const auto elapsedNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(),
                                static_cast<std::uint64_t>(elapsedNs),
                                DiffPortStats(portStatsBefore, portStatsAfter), true, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_NonBlocking)->Unit(benchmark::kMicrosecond);
