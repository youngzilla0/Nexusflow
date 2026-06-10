#include "common/LockBaseQueue.hpp"
#include "QueueBenchmarkCommon.hpp"
#include <atomic>
#include <benchmark/benchmark.h>
#include <thread>

using namespace nexusflow;
using queue_bench::Blob2K;
using queue_bench::OpStats;
using queue_bench::SharedPayloadPtr;
using queue_bench::kPoolMask;
using queue_bench::kQueueCap;

static void BM_LockBaseQueueInt_PushPop_SingleThread(benchmark::State& state) {
    LockBaseQueue<int> q(1024);
    int value = 42;

    for (auto _ : state) {
        q.TryPush(value);
        int out = 0;
        q.TryPop(out);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_LockBaseQueueInt_PushPop_SingleThread);

static void BM_LockBaseQueueInt_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockBaseQueue<int>* q = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        q = new LockBaseQueue<int>(kQueueCap);
        exit_count.store(0, std::memory_order_relaxed);
    }

    while (q == nullptr) {
        std::this_thread::yield();
    }

    const bool is_producer = (state.thread_index() % 2 == 0);
    OpStats stats;
    int value = 42;

    for (auto _ : state) {
        if (is_producer) {
            if (q->TryPush(value)) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
        } else {
            int out = 0;
            if (q->TryPop(out)) {
                ++stats.pop_ok;
                benchmark::DoNotOptimize(out);
            } else {
                ++stats.pop_fail;
            }
        }
    }

    if (!is_producer) {
        state.SetItemsProcessed(static_cast<int64_t>(stats.pop_ok));
    }
    queue_bench::publishCounters(state, stats);

    if (exit_count.fetch_add(1, std::memory_order_acq_rel) == state.threads() - 1) {
        delete q;
        q = nullptr;
    }
}

BENCHMARK(BM_LockBaseQueueInt_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockBaseQueueSharedPtr_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockBaseQueue<SharedPayloadPtr>* q = nullptr;
    static std::vector<SharedPayloadPtr>* pool = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        q = new LockBaseQueue<SharedPayloadPtr>(kQueueCap);
        pool = queue_bench::makeSharedPayloadPool();
        exit_count.store(0, std::memory_order_relaxed);
    }

    while (q == nullptr || pool == nullptr) {
        std::this_thread::yield();
    }

    const bool is_producer = (state.thread_index() % 2 == 0);
    OpStats stats;
    std::size_t index = static_cast<std::size_t>(state.thread_index());

    for (auto _ : state) {
        if (is_producer) {
            const SharedPayloadPtr item = (*pool)[index & kPoolMask];
            if (q->TryPush(item)) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
            ++index;
        } else {
            SharedPayloadPtr out;
            if (q->TryPop(out)) {
                ++stats.pop_ok;
                benchmark::DoNotOptimize(out);
            } else {
                ++stats.pop_fail;
            }
        }
    }

    if (!is_producer) {
        state.SetItemsProcessed(static_cast<int64_t>(stats.pop_ok));
    }
    queue_bench::publishCounters(state, stats);

    if (exit_count.fetch_add(1, std::memory_order_acq_rel) == state.threads() - 1) {
        delete q;
        delete pool;
        q = nullptr;
        pool = nullptr;
    }
}

BENCHMARK(BM_LockBaseQueueSharedPtr_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockBaseQueueBlob2K_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockBaseQueue<Blob2K>* q = nullptr;
    static std::vector<Blob2K>* pool = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        q = new LockBaseQueue<Blob2K>(kQueueCap);
        pool = queue_bench::makeBlobPool();
        exit_count.store(0, std::memory_order_relaxed);
    }

    while (q == nullptr || pool == nullptr) {
        std::this_thread::yield();
    }

    const bool is_producer = (state.thread_index() % 2 == 0);
    OpStats stats;
    std::size_t index = static_cast<std::size_t>(state.thread_index());

    for (auto _ : state) {
        if (is_producer) {
            const Blob2K& item = (*pool)[index & kPoolMask];
            if (q->TryPush(item)) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
            ++index;
        } else {
            Blob2K out;
            if (q->TryPop(out)) {
                ++stats.pop_ok;
                benchmark::DoNotOptimize(out);
            } else {
                ++stats.pop_fail;
            }
        }
    }

    if (!is_producer) {
        state.SetItemsProcessed(static_cast<int64_t>(stats.pop_ok));
    }
    queue_bench::publishCounters(state, stats);

    if (exit_count.fetch_add(1, std::memory_order_acq_rel) == state.threads() - 1) {
        delete q;
        delete pool;
        q = nullptr;
        pool = nullptr;
    }
}

BENCHMARK(BM_LockBaseQueueBlob2K_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);
