#include "common/LockFreeQueue.hpp"
#include <atomic>
#include <benchmark/benchmark.h>
#include <thread>

using namespace nexusflow;

// 测试上层封装队列 (附带统计和 Drop 策略) 性能
static void BM_NodeQueue_DropTail(benchmark::State& state) {
    static LockFreeNodeQueue<int>* shared_nq = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        LockFreeNodeQueue<int>::Config cfg;
        cfg.capacity = 65536;
        cfg.drop_policy = LockFreeDropPolicy::DropTail;
        cfg.track_statistics = true; // 开启统计看看开销大不大
        shared_nq = new LockFreeNodeQueue<int>(std::move(cfg));
        exit_count.store(0, std::memory_order_relaxed);
    }

    while (shared_nq == nullptr) {
        std::this_thread::yield();
    }

    bool isProducer = (state.thread_index() % 2 == 0);
    uint64_t itemsProcessed = 0;

    for (auto _ : state) {
        if (isProducer) {
            shared_nq->push(42); // 调用上层 push 接口
        } else {
            auto opt = shared_nq->tryPop();
            if (opt.hasValue()) {
                itemsProcessed++;
                benchmark::DoNotOptimize(opt);
            }
        }
    }

    if (!isProducer) {
        state.SetItemsProcessed(itemsProcessed);
    }

    if (exit_count.fetch_add(1, std::memory_order_acq_rel) == state.threads() - 1) {
        delete shared_nq;
        shared_nq = nullptr;
    }
}

// 注册测试：范围为 2, 4, 8, 16 线程
BENCHMARK(BM_NodeQueue_DropTail)->ThreadRange(2, 16)->UseRealTime();

// 模拟常见的共享指针数据
struct Payload {
    uint64_t frame_id;
    char raw_data[1024]; // 模拟 1KB 的数据负载
};
using PortDataPtr = std::shared_ptr<Payload>;

// =============================================================================
// 1. 底层核心队列压测 (LockFreeMPMCQueue)
// 用于测量 Vyukov 算法本身的极限吞吐量
// =============================================================================
static void BM_CoreQueue_Throughput(benchmark::State& state) {
    static LockFreeMPMCQueue<PortDataPtr>* shared_q = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        shared_q = new LockFreeMPMCQueue<PortDataPtr>(16384);
        exit_count.store(0);
    }

    while (!shared_q) std::this_thread::yield();

    bool isProducer = (state.thread_index() % 2 == 0);
    uint64_t itemsProcessed = 0;
    auto data = std::make_shared<Payload>();

    for (auto _ : state) {
        if (isProducer) {
            // 纯写入，不考虑满载
            shared_q->tryPush(std::move(data));
            // 压测需要循环利用数据，重新创建一个
            data = std::make_shared<Payload>();
        } else {
            PortDataPtr out;
            if (shared_q->tryPop(out)) {
                itemsProcessed++;
            }
        }
    }

    if (!isProducer) state.SetItemsProcessed(itemsProcessed);

    if (exit_count.fetch_add(1) == state.threads() - 1) {
        delete shared_q;
        shared_q = nullptr;
    }
}

// =============================================================================
// 2. 节点队列压测 - 正常吞吐 (DropTail 策略)
// 测量上层封装 (统计、Optional、Policy 分发) 带来的额外开销
// =============================================================================
static void BM_NodeQueue_Normal_Throughput(benchmark::State& state) {
    static LockFreeNodeQueue<PortDataPtr>* shared_nq = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        LockFreeNodeQueue<PortDataPtr>::Config cfg;
        cfg.capacity = 16384;
        cfg.drop_policy = LockFreeDropPolicy::DropTail;
        cfg.track_statistics = true; // 开启统计以观察真实开销
        shared_nq = new LockFreeNodeQueue<PortDataPtr>(cfg);
        exit_count.store(0);
    }

    while (!shared_nq) std::this_thread::yield();

    bool isProducer = (state.thread_index() % 2 == 0);
    uint64_t itemsProcessed = 0;
    auto data = std::make_shared<Payload>();

    for (auto _ : state) {
        if (isProducer) {
            shared_nq->push(std::move(data));
            data = std::make_shared<Payload>();
        } else {
            // 使用你的 Optional 接口
            nexusflow::Optional<PortDataPtr> opt = shared_nq->tryPop();
            if (opt.hasValue()) {
                itemsProcessed++;
            }
        }
    }

    if (!isProducer) state.SetItemsProcessed(itemsProcessed);

    if (exit_count.fetch_add(1) == state.threads() - 1) {
        delete shared_nq;
        shared_nq = nullptr;
    }
}

// =============================================================================
// 3. 节点队列压测 - 溢出过载 (DropHead 策略)
// 这是一个极端场景：生产者远快于消费者。
// 测量 forcePush (挤出最老数据) 的逻辑在激烈竞争下的性能。
// =============================================================================
static void BM_NodeQueue_Overload_DropHead(benchmark::State& state) {
    static LockFreeNodeQueue<PortDataPtr>* shared_nq = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        LockFreeNodeQueue<PortDataPtr>::Config cfg;
        cfg.capacity = 128; // 故意设小，强制触发溢出
        cfg.drop_policy = LockFreeDropPolicy::DropHead;
        shared_nq = new LockFreeNodeQueue<PortDataPtr>(cfg);
        exit_count.store(0);
    }

    while (!shared_nq) std::this_thread::yield();

    // 模拟非对称压力：3个生产者对1个消费者
    bool isProducer = (state.thread_index() % 4 != 0);
    uint64_t itemsProcessed = 0;
    auto data = std::make_shared<Payload>();

    for (auto _ : state) {
        if (isProducer) {
            // 在 DropHead 模式下，push 永远返回 true (因为它会挤掉老的)
            shared_nq->push(std::move(data));
            data = std::make_shared<Payload>();
            itemsProcessed++; // 生产者也记数，看一共塞进去多少
        } else {
            nexusflow::Optional<PortDataPtr> opt = shared_nq->tryPop();
            if (opt.hasValue()) {
                itemsProcessed++;
            }
            // 模拟消费者比较慢（比如在做 AI 推理）
            // 如果不加耗时，消费者太快就触发不了溢出了
            for (volatile int i = 0; i < 100; ++i)
                ;
        }
    }

    state.SetItemsProcessed(itemsProcessed);

    if (exit_count.fetch_add(1) == state.threads() - 1) {
        delete shared_nq;
        shared_nq = nullptr;
    }
}

// =============================================================================
// 注册测试
// =============================================================================

// 注册底层引擎测试
BENCHMARK(BM_CoreQueue_Throughput)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

// 注册标准业务测试 (SPSC, MPMC)
BENCHMARK(BM_NodeQueue_Normal_Throughput)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

// 注册过载测试 (测试 DropHead 的 CAS 自旋惩罚)
BENCHMARK(BM_NodeQueue_Overload_DropHead)
    ->Threads(4) // 3 产 1 消
    ->Threads(8) // 6 产 2 消
    ->UseRealTime()
    ->Unit(benchmark::kNanosecond);