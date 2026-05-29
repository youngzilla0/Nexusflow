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
    PassThroughModule(std::string name) : Module(std::move(name)) {}
    void Process(Message& msg) override { Broadcast(msg); }
};

class SourceModule : public Module {
public:
    SourceModule(std::string name) : Module(std::move(name)), mRunning(false), mCounter(0) {}

    // 手动触发发送一条消息
    void GenerateOne() {
        uint64_t now = GetNowNs();
        Message msg(now); // 消息内容为当前时间戳
        Broadcast(msg);
        mCounter++;
    }

    void Process(Message&) override {
        // 如果框架自动回调 Process，则在这里产生数据
        if (mRunning.load()) {
            GenerateOne();
        }
    }

    std::atomic<bool> mRunning{false};
    std::atomic<uint64_t> mCounter{0};
};

// -----------------------------------------------------------------------------
// BM_Pipeline_Latency: 测量平均单次端到端延迟
// -----------------------------------------------------------------------------
static void BM_Pipeline_Latency(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1");
    auto pass2 = std::make_shared<PassThroughModule>("Pass2");
    auto sink = std::make_shared<SinkModule>("Sink");

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
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
            std::this_thread::yield();
        }

        // 计算平均延迟：总延迟 / 总接收数
        double avg_latency = static_cast<double>(sink->GetTotalLatencyNs()) / sink->GetMessageCount();
        state.counters["AvgLatencyNs"] = avg_latency;
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Latency)->Unit(benchmark::kMicrosecond);

// -----------------------------------------------------------------------------
// BM_Pipeline_Throughput: 测量系统极限吞吐量
// -----------------------------------------------------------------------------
static void BM_Pipeline_Throughput(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source");
    auto pass1 = std::make_shared<PassThroughModule>("Pass1");
    auto pass2 = std::make_shared<PassThroughModule>("Pass2");
    auto sink = std::make_shared<SinkModule>("Sink");

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        auto start_time = steady_clock::now();

        // 在 50ms 内疯狂发送消息
        while (duration_cast<milliseconds>(steady_clock::now() - start_time).count() < 50) {
            source->GenerateOne();
        }

        // 记录这段时间处理的总量
        uint64_t count = sink->GetMessageCount();
        state.SetItemsProcessed(count);
    }

    pipeline->Stop();
}
BENCHMARK(BM_Pipeline_Throughput)->Unit(benchmark::kMillisecond);

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
        q.tryPush(msg);
        Message popped;
        q.tryPop(popped);
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
            q.push(msg);
        }
    });

    for (auto _ : state) {
        Message popped;
        if (q.waitAndPop(popped)) {
            count++;
        }
        benchmark::DoNotOptimize(popped);
    }

    running = false;
    producer.join();
}
BENCHMARK(BM_ConcurrentQueue_Throughput)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();