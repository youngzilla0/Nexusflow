#include "common/LockFreeQueue.hpp"
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

static void BM_LockFreeMPMCQueueInt_PushPop_SingleThread(benchmark::State& state) {
    LockFreeMPMCQueue<int> q(1024);
    int value = 42;

    for (auto _ : state) {
        q.tryPush(std::move(value));
        int out = 0;
        q.tryPop(out);
        benchmark::DoNotOptimize(out);
    }
}
BENCHMARK(BM_LockFreeMPMCQueueInt_PushPop_SingleThread);

static void BM_LockFreeMPMCQueueInt_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockFreeMPMCQueue<int>* q = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        q = new LockFreeMPMCQueue<int>(kQueueCap);
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
            if (q->tryPush(std::move(value))) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
        } else {
            int out = 0;
            if (q->tryPop(out)) {
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

BENCHMARK(BM_LockFreeMPMCQueueInt_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockFreeMPMCQueueSharedPtr_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockFreeMPMCQueue<SharedPayloadPtr>* q = nullptr;
    static std::vector<SharedPayloadPtr>* pool = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        q = new LockFreeMPMCQueue<SharedPayloadPtr>(kQueueCap);
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
            SharedPayloadPtr item = (*pool)[index & kPoolMask];
            if (q->tryPush(std::move(item))) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
            ++index;
        } else {
            SharedPayloadPtr out;
            if (q->tryPop(out)) {
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

BENCHMARK(BM_LockFreeMPMCQueueSharedPtr_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockFreeMPMCQueueBlob2K_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockFreeMPMCQueue<Blob2K>* q = nullptr;
    static std::vector<Blob2K>* pool = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        q = new LockFreeMPMCQueue<Blob2K>(kQueueCap);
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
            Blob2K item = (*pool)[index & kPoolMask];
            if (q->tryPush(std::move(item))) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
            ++index;
        } else {
            Blob2K out;
            if (q->tryPop(out)) {
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

BENCHMARK(BM_LockFreeMPMCQueueBlob2K_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockFreeNodeQueueInt_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockFreeNodeQueue<int>* q = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        LockFreeNodeQueue<int>::Config cfg;
        cfg.capacity = kQueueCap;
        cfg.drop_policy = LockFreeDropPolicy::DropTail;
        cfg.track_statistics = false;
        q = new LockFreeNodeQueue<int>(cfg);
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
            if (q->push(value)) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
        } else {
            auto out = q->tryPop();
            if (out.hasValue()) {
                ++stats.pop_ok;
                benchmark::DoNotOptimize(out.value());
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

BENCHMARK(BM_LockFreeNodeQueueInt_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockFreeNodeQueueSharedPtr_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockFreeNodeQueue<SharedPayloadPtr>* q = nullptr;
    static std::vector<SharedPayloadPtr>* pool = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        LockFreeNodeQueue<SharedPayloadPtr>::Config cfg;
        cfg.capacity = kQueueCap;
        cfg.drop_policy = LockFreeDropPolicy::DropTail;
        cfg.track_statistics = false;
        q = new LockFreeNodeQueue<SharedPayloadPtr>(cfg);
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
            SharedPayloadPtr item = (*pool)[index & kPoolMask];
            if (q->push(std::move(item))) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
            ++index;
        } else {
            auto out = q->tryPop();
            if (out.hasValue()) {
                ++stats.pop_ok;
                benchmark::DoNotOptimize(out.value());
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

BENCHMARK(BM_LockFreeNodeQueueSharedPtr_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);

static void BM_LockFreeNodeQueueBlob2K_Throughput_ProducerConsumer(benchmark::State& state) {
    static LockFreeNodeQueue<Blob2K>* q = nullptr;
    static std::vector<Blob2K>* pool = nullptr;
    static std::atomic<int> exit_count{0};

    if (state.thread_index() == 0) {
        LockFreeNodeQueue<Blob2K>::Config cfg;
        cfg.capacity = kQueueCap;
        cfg.drop_policy = LockFreeDropPolicy::DropTail;
        cfg.track_statistics = false;
        q = new LockFreeNodeQueue<Blob2K>(cfg);
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
            Blob2K item = (*pool)[index & kPoolMask];
            if (q->push(std::move(item))) {
                ++stats.push_ok;
            } else {
                ++stats.push_fail;
            }
            ++index;
        } else {
            auto out = q->tryPop();
            if (out.hasValue()) {
                ++stats.pop_ok;
                benchmark::DoNotOptimize(out.value());
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

BENCHMARK(BM_LockFreeNodeQueueBlob2K_Throughput_ProducerConsumer)->ThreadRange(2, 16)->UseRealTime()->Unit(benchmark::kNanosecond);
