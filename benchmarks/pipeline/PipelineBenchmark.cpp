#include "../../src/common/ConcurrentQueue.hpp"
#include "../../src/utils/logging.hpp"
#include <benchmark/benchmark.h>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>
#include <nexusflow/Pipeline.hpp>
#include <nexusflow/PipelineBuilder.hpp>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

using namespace nexusflow;
using namespace std::chrono;

// -----------------------------------------------------------------------------
// Test Modules
// -----------------------------------------------------------------------------

inline uint64_t GetNowNs() { return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }

class SinkModule : public Module {
public:
    SinkModule(std::string name) : Module(std::move(name)) {}

    void Process(Message& msg) override {
        uint64_t now = GetNowNs();
        if (auto* start_time = msg.BorrowPtr<uint64_t>()) {
            // 计算单条路径的延迟：当前时间 - 发送时间
            mTotalLatencyNs += (now - *start_time);
        }
        mMessageCount++;
    }

    uint64_t GetMessageCount() const { return mMessageCount.load(); }
    uint64_t GetTotalLatencyNs() const { return mTotalLatencyNs.load(); }

    void Reset() {
        mMessageCount = 0;
        mTotalLatencyNs = 0;
    }

    std::atomic<uint64_t> mMessageCount{0};
    std::atomic<uint64_t> mTotalLatencyNs{0};
};

class PassThroughModule : public Module {
public:
    PassThroughModule(std::string name, bool blocking = true, int simulateLatencyUs = 0)
        : Module(std::move(name)), m_blocking(blocking), m_simulateLatencyUs(simulateLatencyUs) {}

    void Process(Message& msg) override {
        if (m_simulateLatencyUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(m_simulateLatencyUs));
        }
        Broadcast(msg, m_blocking);
    }

    bool m_blocking;
    int m_simulateLatencyUs{0};
};

class SourceModule : public Module {
public:
    SourceModule(std::string name, bool blocking = true) : Module(std::move(name)), mRunning(false), mCounter(0), m_blocking(blocking) {}

    // 手动触发发送一条消息
    void GenerateOne() {
        uint64_t now = GetNowNs();
        Message msg(now); // 消息内容为当前时间戳
        Broadcast(msg, m_blocking);
        mCounter++;
        // LOG_INFO("Send message, current count: {}", mCounter);
    }

    void Process(Message&) override {
    }

    std::atomic<bool> mRunning{false};
    std::atomic<uint64_t> mCounter{0};
    bool m_blocking{true};
};

// -----------------------------------------------------------------------------
// BM_Pipeline_Latency: 测量平均单次端到端延迟
// -----------------------------------------------------------------------------
static void BM_Pipeline_Latency(benchmark::State& state) {
    // Latency test: use blocking send for reliable delivery
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    // Create pipeline config with optimized settings
    PipelineConfig config;
    config.maxBatchSize = 32;
    config.batchTimeoutMs = 0;  // Low latency: no batching wait
    config.queueSize = 100;

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

        // 发送一定数量的消息来测试平均延迟
        const int test_count = 1000;
        for (int i = 0; i < test_count; ++i) {
            source->GenerateOne();
        }

        // 等待所有消息到达 Sink (菱形拓扑，Sink 应该收到 2 * test_count 条)
        while (sink->GetMessageCount() < test_count * 2) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }

        // 计算平均延迟：总延迟 / 总接收数
        double avg_latency = static_cast<double>(sink->GetTotalLatencyNs()) / sink->GetMessageCount();
        state.counters["AvgLatencyNs"] = avg_latency;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Latency)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput: 测量系统稳态吞吐量
// 统计 Source 发送总数 vs Sink 接收总数，计算真实 throughput
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput(benchmark::State& state) {
    // Use blocking send to measure true steady-state throughput
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.maxBatchSize = 64;
    config.batchTimeoutMs = 0;  // Low latency mode
    config.queueSize = 10000;   // Large queue to avoid drops

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

        // 持续发送消息直到时间到期,全速发送不限制速率
        // 测试 pipeline 的真实最大吞吐量
        while (steady_clock::now() - start_time < std::chrono::seconds(1)) {
            source->GenerateOne();
        }

        auto end_time = steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();

        // 计算吞吐量: Sink 收到消息数 / 总时间
        // 菱形拓扑: Source -> [Pass1, Pass2] -> Sink, 所以 Sink 收到的是 Sent 的 2 倍
        uint64_t sent = source->mCounter.load();
        uint64_t received = sink->GetMessageCount();
        uint64_t dropped = sent > 0 ? sent - (received / 2) : 0;  // 去掉广播倍增后的真实发送数

        double throughput = (received * 1e9) / elapsed;

        state.SetItemsProcessed(received);
        state.counters["Throughput"] = throughput;
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
        // 修正 throughput: 菱形拓扑每个消息被复制2份
        state.counters["TrueThroughput"] = (received / 2 * 1e9) / elapsed;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Linear_Throughput: 线性拓扑吞吐量测试
// Source -> Pass1 -> Pass2 -> Sink (无广播复制，每条消息只走一条路径)
// -----------------------------------------------------------------------------
static void BM_Pipeline_Linear_Throughput(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
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
                        .Connect("Pass1", "Pass2")
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
        uint64_t dropped = sent > received ? sent - received : 0;

        double throughput = (received * 1e9) / elapsed;

        state.SetItemsProcessed(received);
        state.counters["Throughput"] = throughput;
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Linear_Throughput)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Linear_Throughput_WithLatency: 线性拓扑 + 处理延迟
// -----------------------------------------------------------------------------
static void BM_Pipeline_Linear_Throughput_WithLatency(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true, 100);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true, 100);
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
                        .Connect("Pass1", "Pass2")
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
        uint64_t dropped = sent > received ? sent - received : 0;

        double throughput = (received * 1e9) / elapsed;

        state.SetItemsProcessed(received);
        state.counters["Throughput"] = throughput;
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Linear_Throughput_WithLatency)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput_SingleOutput: Diamond 但只有一个下游
// Source -> Pass1 -> Sink, Pass2 存在但不连接
// 用来验证是否是"两个输出"导致的竞争
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput_SingleOutput(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);  // 不连接
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
                        .Connect("Source", "Pass1")  // 只连接到 Pass1
                        .Connect("Pass1", "Sink")
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
        uint64_t dropped = sent > received ? sent - received : 0;

        double throughput = (received * 1e9) / elapsed;

        state.SetItemsProcessed(received);
        state.counters["Throughput"] = throughput;
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_SingleOutput)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput_LargeSinkQueue: Diamond 拓扑，Sink 队列 100000
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput_LargeSinkQueue(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
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
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
        state.counters["TrueThroughput"] = (received / 2 * 1e9) / elapsed;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_LargeSinkQueue)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput_WithLatency: 带处理延迟的吞吐量测试
// 每个 PassThroughModule 模拟 100us 处理延迟
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput_WithLatency(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true, 100);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true, 100);
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
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
        state.counters["TrueThroughput"] = (received / 2 * 1e9) / elapsed;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_WithLatency)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Message_COW_Copy: Benchmark Message COW copy performance
// -----------------------------------------------------------------------------
static void BM_Message_COW_Copy(benchmark::State& state) {
    auto original = MakeMessage(std::vector<int>(100, 42));

    for (auto _ : state) {
        auto copy = original;
        benchmark::DoNotOptimize(copy);
    }
}
BENCHMARK(BM_Message_COW_Copy);

static void BM_Message_COW_Mutate(benchmark::State& state) {
    auto original = MakeMessage(std::vector<int>(100, 42));
    auto copy = original;

    for (auto _ : state) {
        if (auto* vec = copy.MutPtr<std::vector<int>>()) {
            (*vec)[0]++;
            benchmark::DoNotOptimize(vec);
        }
    }
}
BENCHMARK(BM_Message_COW_Mutate);

// -----------------------------------------------------------------------------
// BM_Message_Borrow_Mut: Benchmark Message access patterns
// -----------------------------------------------------------------------------
static void BM_Message_Borrow(benchmark::State& state) {
    auto msg = MakeMessage(std::vector<int>(100, 42));

    long sum = 0;
    for (auto _ : state) {
        if (auto* vec = msg.BorrowPtr<std::vector<int>>()) {
            for (int i = 0; i < 100; ++i) {
                sum += (*vec)[i];
            }
        }
        benchmark::DoNotOptimize(sum);
    }
}
BENCHMARK(BM_Message_Borrow);

static void BM_Message_Mut(benchmark::State& state) {
    auto msg = MakeMessage(std::vector<int>(100, 42));

    for (auto _ : state) {
        if (auto* vec = msg.MutPtr<std::vector<int>>()) {
            for (int i = 0; i < 100; ++i) {
                (*vec)[i]++;
            }
        }
        benchmark::DoNotOptimize(msg);
    }
}
BENCHMARK(BM_Message_Mut);

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
        if (q.WaitAndPopFor(popped, std::chrono::milliseconds(1))) {
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
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
        state.counters["TrueThroughput"] = (received / 2 * 1e9) / elapsed;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_Warmup)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput_NonBlocking: Diamond 拓扑，所有模块用 non-blocking send
// 验证 blocking vs non-blocking 对吞吐量的影响
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput_NonBlocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", false);  // non-blocking
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
        state.counters["Sent"] = sent;
        state.counters["Dropped"] = dropped;
        state.counters["DropsPct"] = sent > 0 ? (dropped * 100.0 / sent) : 0;
        state.counters["TrueThroughput"] = (received / 2 * 1e9) / elapsed;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput_NonBlocking)->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
