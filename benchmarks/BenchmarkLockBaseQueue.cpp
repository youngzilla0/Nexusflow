#include "common/LockBaseQueue.hpp"
#include "nexusflow/Message.hpp"
#include <benchmark/benchmark.h>
#include <chrono>
#include <cstdint>
#include <thread>

using namespace nexusflow;
using namespace std::chrono;

// -----------------------------------------------------------------------------
// BM_LockBaseQueue_PushPop: Benchmark queue operations
// -----------------------------------------------------------------------------
static void BM_LockBaseQueue_PushPop(benchmark::State& state) {
    LockBaseQueue<Message> q(1000);
    auto msg = MakeMessage(42);

    for (auto _ : state) {
        q.TryPush(msg);
        Message popped;
        q.TryPop(popped);
        benchmark::DoNotOptimize(popped);
    }
}
BENCHMARK(BM_LockBaseQueue_PushPop);

static void BM_LockBaseQueue_Throughput(benchmark::State& state) {
    LockBaseQueue<Message> q;
    std::atomic<bool> running{true};
    uint64_t count = 0;

    std::thread producer([&]() {
        auto msg = MakeMessage(42);
        while (running.load(std::memory_order_relaxed)) {
            q.PushFor(msg, std::chrono::nanoseconds(100));
        }
    });

    // Give producer time to fill the queue
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    for (auto _ : state) {
        Message popped;
        if (q.TryPop(popped)) {
            count++;
            benchmark::DoNotOptimize(popped);
        }
    }
    running.store(false, std::memory_order_relaxed);
    producer.join();
    state.SetItemsProcessed(count);
}
BENCHMARK(BM_LockBaseQueue_Throughput)
    ->UseRealTime() // 多线程测试必须使用真实时间(Wall-clock time)，而非CPU时间
    ->Unit(benchmark::kMillisecond);

// 1. 使用 Fixture 来管理共享的队列状态
// 这样可以确保在多线程测试时，所有线程都在操作同一个队列
class QueueFixture : public benchmark::Fixture {
public:
    // 使用静态指针在同一批次的不同线程间共享队列
    static LockBaseQueue<Message>* shared_q;

    // SetUp 会在每个线程开始时调用
    void SetUp(const benchmark::State& state) override {
        // 我们只让 0 号线程负责初始化队列，框架内部会确保内存同步
        if (state.thread_index() == 0) {
            shared_q = new LockBaseQueue<Message>();
        }
    }

    // TearDown 会在每个线程结束时调用
    void TearDown(const benchmark::State& state) override {
        // 只让 0 号线程负责销毁队列
        if (state.thread_index() == 0) {
            delete shared_q;
            shared_q = nullptr;
        }
    }
};

// 初始化静态成员变量
LockBaseQueue<Message>* QueueFixture::shared_q = nullptr;

// 2. 定义 Benchmark 核心逻辑
BENCHMARK_DEFINE_F(QueueFixture, LockBaseQueue_Throughput)(benchmark::State& state) {
    // 核心改进：根据线程 ID 划分角色
    // 偶数号线程 (0, 2, 4...) 做生产者，奇数号线程 (1, 3, 5...) 做消费者
    bool is_producer = (state.thread_index() % 2 == 0);

    auto msg = MakeMessage(42);
    uint64_t items_processed = 0; // 当前线程处理的消息数

    for (auto _ : state) {
        if (is_producer) {
            // 生产者逻辑
            // 尝试推入，设定一个合理的超时防止死锁
            shared_q->PushFor(msg, std::chrono::nanoseconds(100));
        } else {
            // 消费者逻辑
            Message popped;
            if (shared_q->TryPop(popped)) {
                items_processed++;
                benchmark::DoNotOptimize(popped); // 防止编译器把 pop 出的数据优化掉
            }
        }
    }

    // 只在“消费者”端统计处理量
    // 避免“发一次、收一次”被算作处理了 2 个 item
    if (!is_producer) {
        // 框架会自动将所有消费者线程上报的 items_processed 加起来，除以总时间！
        state.SetItemsProcessed(items_processed);
    }
}

// 3. 注册测试并配置线程数
// ->ThreadRange(2, 8) 表示框架会自动跑三轮测试：
// 第一轮：2个线程 (1个生产者，1个消费者)
// 第二轮：4个线程 (2个生产者，2个消费者)
// 第三轮：8个线程 (4个生产者，4个消费者)
BENCHMARK_REGISTER_F(QueueFixture, LockBaseQueue_Throughput)
    ->ThreadRange(2, 8)
    ->UseRealTime() // 并发测试必须用真实时间
    ->Unit(benchmark::kMillisecond);