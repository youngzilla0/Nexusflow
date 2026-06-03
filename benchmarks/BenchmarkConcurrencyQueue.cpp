#include "common/ConcurrentQueue.hpp"
#include "nexusflow/Message.hpp"
#include "nexusflow/Pipeline.hpp"
#include <benchmark/benchmark.h>
#include <cstdint>
#include <thread>

using namespace nexusflow;
using namespace std::chrono;

// -----------------------------------------------------------------------------
// BM_ConcurrentQueue_PushPop: Benchmark queue operations
// -----------------------------------------------------------------------------
static void BM_ConcurrentQueue_PushPop(benchmark::State& state) {
    ConcurrentQueue<Message> q(1000);
    auto msg = MakeMessage(42);

    for (auto _ : state) {
        q.TryPush(msg);
        Message popped;
        q.TryPop(popped);
        benchmark::DoNotOptimize(popped);
    }
}
BENCHMARK(BM_ConcurrentQueue_PushPop);

static void BM_ConcurrentQueue_Throughput(benchmark::State& state) {
    ConcurrentQueue<Message> q(10000);
    std::atomic<bool> running{true};
    std::atomic<uint64_t> count{0};

    std::thread producer([&]() {
        auto msg = MakeMessage(42);
        while (running.load()) {
            q.Push(msg);
        }
    });

    // Give producer time to fill the queue
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (auto _ : state) {
        Message popped;
        // Use TryPop instead of WaitAndPop to avoid blocking
        if (q.TryPop(popped)) {
            count++;
        }
        benchmark::DoNotOptimize(popped);
    }

    running = false;
    producer.join();
}
BENCHMARK(BM_ConcurrentQueue_Throughput)->Unit(benchmark::kMillisecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput_Warmup: Diamond 拓扑，先预热再测
// 验证是否是测量时机问题
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput_Warmup(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.maxBatchSize = 64;
    config.batchTimeoutMs = 0;
    config.queueSize = 10000;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->mCounter = 0;

        // 预热：发一些消息让 pipeline 达到稳态
        for (int i = 0; i < 100; i++) {
            source->GenerateOne();
        }
        // 等待预热消息全部到达
        while (sink->GetMessageCount() < 200) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        sink->Reset();
        source->mCounter = 0;

        auto start_time = steady_clock::now();
        while (steady_clock::now() - start_time < std::chrono::seconds(1)) {
            source->GenerateOne();
        }

        // 等待所有消息都处理完（加上额外的 500ms 缓冲）
        auto processing_deadline = steady_clock::now() + std::chrono::milliseconds(500);
        while (sink->GetMessageCount() < source->mCounter.load() * 2) {
            if (steady_clock::now() > processing_deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        auto end_time = steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        uint64_t sent = source->mCounter.load();
        uint64_t received = sink->GetMessageCount();
        uint64_t dropped = sent > 0 ? sent - (received / 2) : 0;

        double throughput = (received * 1e9) / elapsed;

        state.SetItemsProcessed(received);
        state.counters["Throughput"] = throughput;
        // 统计发送和接收情况
        state.counters["Sent"] = sent;
        state.counters["Received"] = received;
        state.counters["Dropped"] = dropped;

        // 统计丢包率
        double dropRate = static_cast<double>(dropped) / sent * 100;
        state.counters["DropRate"] = dropRate;

        // 统计每条消息的平均处理时间
        double avg_latency = static_cast<double>(sink->GetTotalLatencyNs()) / received;
        state.counters["AvgLatencyNs"] = avg_latency;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_Warmup)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput_NonBlocking: Diamond 拓扑，所有模块用 non-blocking send
// 验证 blocking vs non-blocking 对吞吐量的影响
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput_NonBlocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", false); // non-blocking
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", false); // non-blocking
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", false); // non-blocking
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.maxBatchSize = 64;
    config.batchTimeoutMs = 0;
    config.queueSize = 10000;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->mCounter = 0;

        auto start_time = steady_clock::now();
        while (steady_clock::now() - start_time < std::chrono::seconds(1)) {
            source->GenerateOne();
        }

        auto end_time = steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        uint64_t sent = source->mCounter.load();
        uint64_t received = sink->GetMessageCount();
        uint64_t dropped = sent > 0 ? sent - (received / 2) : 0;

        double throughput = (received * 1e9) / elapsed;

        state.SetItemsProcessed(received);
        state.counters["Throughput"] = throughput;
        // 统计发送和接收情况
        state.counters["Sent"] = sent;
        state.counters["Received"] = received;
        state.counters["Dropped"] = dropped;

        // 统计丢包率
        double dropRate = static_cast<double>(dropped) / sent * 100;
        state.counters["DropRate"] = dropRate;

        // 统计每条消息的平均处理时间
        double avg_latency = static_cast<double>(sink->GetTotalLatencyNs()) / received;
        state.counters["AvgLatencyNs"] = avg_latency;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_NonBlocking)->Unit(benchmark::kMicrosecond);
