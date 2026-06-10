#ifndef NEXUSFLOW_QUEUE_BENCHMARK_COMMON_HPP
#define NEXUSFLOW_QUEUE_BENCHMARK_COMMON_HPP

#include <array>
#include <benchmark/benchmark.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace queue_bench {

constexpr int kQueueCap = 1 << 14;
constexpr std::size_t kPoolSize = 1 << 10;
constexpr std::size_t kPoolMask = kPoolSize - 1;

struct SharedPayload {
    std::uint64_t frame_id = 0;
    char raw_data[256] = {};
};

using SharedPayloadPtr = std::shared_ptr<SharedPayload>;

struct Blob2K {
    std::uint64_t frame_id = 0;
    std::array<std::uint64_t, 256> words{};
};

template <typename T, typename Factory>
std::vector<T>* makePool(Factory factory) {
    auto* pool = new std::vector<T>();
    pool->reserve(kPoolSize);
    for (std::size_t i = 0; i < kPoolSize; ++i) {
        pool->push_back(factory(i));
    }
    return pool;
}

inline std::vector<SharedPayloadPtr>* makeSharedPayloadPool() {
    return makePool<SharedPayloadPtr>([](std::size_t index) {
        auto item = std::make_shared<SharedPayload>();
        item->frame_id = static_cast<std::uint64_t>(index);
        return item;
    });
}

inline std::vector<Blob2K>* makeBlobPool() {
    return makePool<Blob2K>([](std::size_t index) {
        Blob2K item;
        item.frame_id = static_cast<std::uint64_t>(index);
        item.words[0] = static_cast<std::uint64_t>(index);
        item.words[255] = static_cast<std::uint64_t>(index * 17);
        return item;
    });
}

struct OpStats {
    std::uint64_t push_ok = 0;
    std::uint64_t push_fail = 0;
    std::uint64_t pop_ok = 0;
    std::uint64_t pop_fail = 0;
};

inline void publishCounters(benchmark::State& state, const OpStats& stats) {
    state.counters["push_ok"] = static_cast<double>(stats.push_ok);
    state.counters["push_fail"] = static_cast<double>(stats.push_fail);
    state.counters["pop_ok"] = static_cast<double>(stats.pop_ok);
    state.counters["pop_fail"] = static_cast<double>(stats.pop_fail);
}

} // namespace queue_bench

#endif
