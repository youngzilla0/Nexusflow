#ifndef LOCK_FREE_QUEUE_HPP
#define LOCK_FREE_QUEUE_HPP

#include "Optional.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <new>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

// 跨平台对齐内存分配 (替代 C++17 的 std::align_val_t)
#if defined(_MSC_VER) || defined(__MINGW32__)
#include <malloc.h>
#endif

namespace nexusflow {

// =============================================================================
// 基本类型定义 (替代原本依赖的 frame_metadata.hpp)
// =============================================================================
using FrameId = std::uint64_t;
using StreamId = std::uint32_t;
using Timestamp = std::chrono::steady_clock::time_point;

constexpr FrameId k_invalid_frame_id = static_cast<FrameId>(-1);
constexpr StreamId k_default_stream_id = 0;
constexpr std::size_t k_cache_line_size = 64;

// =============================================================================
// 跨平台内存对齐辅助函数
// =============================================================================
inline void* allocate_aligned(std::size_t size, std::size_t alignment) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    return _aligned_malloc(size, alignment);
#else
    void* ptr = nullptr;
    if (posix_memalign(&ptr, alignment, size) != 0) return nullptr;
    return ptr;
#endif
}

inline void free_aligned(void* ptr) {
#if defined(_MSC_VER) || defined(__MINGW32__)
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

/**
 * @brief Information about a drop event for callbacks/logging
 */
struct DropEvent {
    FrameId frame_id;
    StreamId stream_id;
    Timestamp drop_time;
    std::string node_name;
    std::string port_name;
    std::string reason;
    std::size_t queue_size_before;
    std::size_t queue_size_after;
    std::size_t total_drops;

    DropEvent()
        : frame_id(k_invalid_frame_id),
          stream_id(k_default_stream_id),
          drop_time(std::chrono::steady_clock::now()),
          queue_size_before(0),
          queue_size_after(0),
          total_drops(0) {}

    std::string toString() const {
        return "DropEvent{frame=" + std::to_string(frame_id) + ", stream=" + std::to_string(stream_id) + ", node=" + node_name +
               ", port=" + port_name + ", reason=" + reason + "}";
    }
};

using DropEventCallback = std::function<void(const DropEvent&)>;

enum class LockFreeDropPolicy {
    DropTail,
    DropHead,
    KeepLatest,
};

struct LockFreeQueueStatistics {
    std::atomic<std::uint64_t> total_pushed{0};
    std::atomic<std::uint64_t> total_popped{0};
    std::atomic<std::uint64_t> total_dropped{0};
    std::atomic<std::uint64_t> total_rejected{0};
    std::atomic<std::uint64_t> peak_size{0};
    std::chrono::steady_clock::time_point created_time{std::chrono::steady_clock::now()};

    void reset() {
        total_pushed.store(0, std::memory_order_relaxed);
        total_popped.store(0, std::memory_order_relaxed);
        total_dropped.store(0, std::memory_order_relaxed);
        total_rejected.store(0, std::memory_order_relaxed);
        peak_size.store(0, std::memory_order_relaxed);
        created_time = std::chrono::steady_clock::now();
    }

    double dropRate() const {
        auto pushed = total_pushed.load(std::memory_order_relaxed);
        auto dropped = total_dropped.load(std::memory_order_relaxed);
        if (pushed == 0) return 0.0;
        return static_cast<double>(dropped) / static_cast<double>(pushed) * 100.0;
    }

    double throughput() const {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - created_time).count();
        if (elapsed == 0) return 0.0;
        return static_cast<double>(total_popped.load(std::memory_order_relaxed)) / static_cast<double>(elapsed);
    }
};

template <typename T>
class LockFreeQueue {
    static_assert(std::is_move_constructible<T>::value, "LockFreeQueue<T> requires T to be move constructible");

public:
    using value_type = T;
    using size_type = std::size_t;

    explicit LockFreeQueue(size_type min_capacity)
        : m_capacity(roundUpPowerOf2(min_capacity < 2 ? 2 : min_capacity)),
          m_mask(m_capacity - 1),
          m_buffer(static_cast<Cell*>(allocate_aligned(sizeof(Cell) * m_capacity, k_cache_line_size))),
          m_enqueuePos(0),
          m_dequeuePos(0) {
        if (!m_buffer) throw std::bad_alloc();

        for (size_type i = 0; i < m_capacity; ++i) {
            new (&m_buffer[i]) Cell();
            m_buffer[i].sequence.store(i, std::memory_order_relaxed);
        }
    }

    ~LockFreeQueue() {
        T dummy;
        while (tryPop(dummy)) {
        }
        for (size_type i = 0; i < m_capacity; ++i) {
            m_buffer[i].~Cell();
        }
        free_aligned(m_buffer);
    }

    LockFreeQueue(const LockFreeQueue&) = delete;
    LockFreeQueue& operator=(const LockFreeQueue&) = delete;
    LockFreeQueue(LockFreeQueue&&) = delete;
    LockFreeQueue& operator=(LockFreeQueue&&) = delete;

    bool tryPush(T&& item) {
        Cell* cell;
        size_type pos = m_enqueuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & m_mask];
            size_type seq = cell->sequence.load(std::memory_order_acquire);
            auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

            if (diff == 0) {
                if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_enqueuePos.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(item);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(T& item) {
        Cell* cell;
        size_type pos = m_dequeuePos.load(std::memory_order_relaxed);

        for (;;) {
            cell = &m_buffer[pos & m_mask];
            size_type seq = cell->sequence.load(std::memory_order_acquire);
            auto diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos + 1);

            if (diff == 0) {
                if (m_dequeuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;
            } else {
                pos = m_dequeuePos.load(std::memory_order_relaxed);
            }
        }

        item = std::move(cell->data);
        cell->sequence.store(pos + m_capacity, std::memory_order_release);
        return true;
    }

    bool forcePush(T item, T& evicted) {
        if (tryPush(std::move(item))) {
            return false;
        }

        constexpr int k_spin_limit = 256;

        if (!tryPop(evicted)) {
            for (int spin = 0; spin < k_spin_limit; ++spin) {
                if (tryPush(std::move(item))) return false;
                spinPause();
            }
            std::this_thread::yield();
            (void)tryPush(std::move(item));
            return false;
        }

        for (int spin = 0; spin < k_spin_limit; ++spin) {
            if (tryPush(std::move(item))) return true;
            spinPause();
        }

        constexpr int k_max_reevictions = 8;
        for (int round = 0; round < k_max_reevictions; ++round) {
            T re_evicted;
            if (tryPop(re_evicted)) {
                for (int spin = 0; spin < k_spin_limit; ++spin) {
                    if (tryPush(std::move(item))) return true;
                    spinPause();
                }
            }
            std::this_thread::yield();
            if (tryPush(std::move(item))) return true;
        }

        while (!tryPush(std::move(item))) {
            T discard;
            (void)tryPop(discard);
            spinPause();
        }
        return true;
    }

    size_type size() const noexcept {
        auto enq = m_enqueuePos.load(std::memory_order_relaxed);
        auto deq = m_dequeuePos.load(std::memory_order_relaxed);
        return enq >= deq ? (enq - deq) : 0;
    }

    bool empty() const noexcept { return size() == 0; }
    bool isFull() const noexcept { return size() >= m_capacity; }
    size_type capacity() const noexcept { return m_capacity; }

    double fillRatio() const noexcept {
        if (m_capacity == 0) return 0.0;
        return static_cast<double>(size()) / static_cast<double>(m_capacity);
    }

private:
    struct alignas(k_cache_line_size) Cell {
        std::atomic<size_type> sequence{0};
        T data{};
    };

    static constexpr size_type roundUpPowerOf2(size_type v) {
        if (v <= 2) return 2;
        std::uint64_t val = v - 1;
        val |= val >> 1;
        val |= val >> 2;
        val |= val >> 4;
        val |= val >> 8;
        val |= val >> 16;
        val |= val >> 32; // C++14 branchless way to handle 64-bit sizes safely
        return static_cast<size_type>(val + 1);
    }

    static void spinPause() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
        _mm_pause(); // for MSVC
#else
        __builtin_ia32_pause();
#endif
#elif defined(__aarch64__) || defined(_M_ARM64)
        asm volatile("yield" ::: "memory");
#else
        std::atomic_thread_fence(std::memory_order_seq_cst);
#endif
    }

    const size_type m_capacity;
    const size_type m_mask;
    Cell* const m_buffer;

    alignas(k_cache_line_size) std::atomic<size_type> m_enqueuePos;
    alignas(k_cache_line_size) std::atomic<size_type> m_dequeuePos;
};

template <typename T>
using LockFreeMPMCQueue = LockFreeQueue<T>;

template <typename T>
class LockFreeNodeQueue {
public:
    using value_type = T;
    using size_type = std::size_t;

    struct Config {
        size_type capacity;
        LockFreeDropPolicy drop_policy;
        size_type keep_latest_n;
        bool track_statistics;
        std::string node_name;
        std::string port_name;

        Config() : capacity(16), drop_policy(LockFreeDropPolicy::DropHead), keep_latest_n(1), track_statistics(true) {}
    };

    explicit LockFreeNodeQueue(size_type capacity) : m_queue(capacity) { m_config.capacity = capacity; }

    explicit LockFreeNodeQueue(Config config) : m_config(std::move(config)), m_queue(m_config.capacity) {}

    ~LockFreeNodeQueue() = default;

    LockFreeNodeQueue(const LockFreeNodeQueue&) = delete;
    LockFreeNodeQueue& operator=(const LockFreeNodeQueue&) = delete;
    LockFreeNodeQueue(LockFreeNodeQueue&&) = delete;
    LockFreeNodeQueue& operator=(LockFreeNodeQueue&&) = delete;

    void setDropPolicy(LockFreeDropPolicy policy) { m_config.drop_policy = policy; }
    LockFreeDropPolicy dropPolicy() const { return m_config.drop_policy; }

    std::string strategyName() const {
        switch (m_config.drop_policy) {
            case LockFreeDropPolicy::DropTail: return "DropTail";
            case LockFreeDropPolicy::DropHead: return "DropHead";
            case LockFreeDropPolicy::KeepLatest: return "KeepLatest" + std::to_string(m_config.keep_latest_n);
        }
        return "Unknown";
    }

    void setDropCallback(DropEventCallback callback) { m_dropCallback = std::move(callback); }

    // 简化后的 accessor (C++14)，返回有效 FrameId 或 k_invalid_frame_id
    using FrameIdAccessor = std::function<Optional<FrameId>(const T&)>;
    void setFrameIdAccessor(FrameIdAccessor accessor) { m_frameIdAccessor = std::move(accessor); }

    bool push(T item) {
        switch (m_config.drop_policy) {
            case LockFreeDropPolicy::DropTail: return pushDropTail(std::move(item));
            case LockFreeDropPolicy::DropHead: return pushDropHead(std::move(item));
            case LockFreeDropPolicy::KeepLatest: return pushKeepLatest(std::move(item));
        }
        return pushDropHead(std::move(item));
    }

    Optional<T> tryPop() {
        T item;
        // 底层依然使用最高效的引用传递
        if (m_queue.tryPop(item)) {
            if (m_config.track_statistics) {
                m_stats.total_popped.fetch_add(1, std::memory_order_relaxed);
            }
            // 成功取出后，利用 move 语义包装成你的 Optional 抛出
            return Optional<T>(std::move(item));
        }
        // 队列为空，返回 nullopt
        return nullopt;
    }
    bool empty() const { return m_queue.empty(); }
    size_type size() const { return m_queue.size(); }
    size_type capacity() const { return m_queue.capacity(); }
    bool isFull() const { return m_queue.isFull(); }
    double fillRatio() const { return m_queue.fillRatio(); }

    void clear() {
        T dummy;
        while (m_queue.tryPop(dummy)) {
        }
    }

    const LockFreeQueueStatistics& statistics() const { return m_stats; }
    void resetStatistics() { m_stats.reset(); }
    const Config& config() const { return m_config; }

private:
    bool pushDropTail(T item) {
        if (m_queue.tryPush(std::move(item))) {
            updatePeakSize();
            if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        if (m_config.track_statistics) m_stats.total_rejected.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    bool pushDropHead(T item) {
        if (m_queue.tryPush(std::move(item))) {
            updatePeakSize();
            if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        constexpr int k_max_evictions = 4;
        constexpr int k_spin_per_eviction = 128;

        for (int round = 0; round < k_max_evictions; ++round) {
            T evicted;
            if (m_queue.tryPop(evicted)) {
                if (m_config.track_statistics) m_stats.total_dropped.fetch_add(1, std::memory_order_relaxed);
                notifyDrop(evicted, "DropHead overflow");
            }
            for (int spin = 0; spin < k_spin_per_eviction; ++spin) {
                if (m_queue.tryPush(std::move(item))) {
                    updatePeakSize();
                    if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
                    return true;
                }
                if ((spin & 31) == 31) std::this_thread::yield();
            }
        }

        while (!m_queue.tryPush(std::move(item))) {
            T discard;
            if (m_queue.tryPop(discard)) {
                if (m_config.track_statistics) m_stats.total_dropped.fetch_add(1, std::memory_order_relaxed);
                notifyDrop(discard, "DropHead overflow (fallback)");
            }
            std::this_thread::yield();
        }
        updatePeakSize();
        if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    bool pushKeepLatest(T item) {
        auto target_keep = m_config.keep_latest_n > 0 ? m_config.keep_latest_n : 1;
        auto current_size = m_queue.size();

        while (current_size >= target_keep) {
            T evicted;
            if (m_queue.tryPop(evicted)) {
                if (m_config.track_statistics) m_stats.total_dropped.fetch_add(1, std::memory_order_relaxed);
                notifyDrop(evicted, "KeepLatest overflow");
                current_size = m_queue.size();
            } else {
                break;
            }
        }

        if (m_queue.tryPush(std::move(item))) {
            updatePeakSize();
            if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
            return true;
        }

        constexpr int k_spin_limit = 128;
        T evicted;
        if (m_queue.tryPop(evicted)) {
            if (m_config.track_statistics) m_stats.total_dropped.fetch_add(1, std::memory_order_relaxed);
            notifyDrop(evicted, "KeepLatest force overflow");
        }

        for (int spin = 0; spin < k_spin_limit; ++spin) {
            if (m_queue.tryPush(std::move(item))) {
                updatePeakSize();
                if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
            if ((spin & 31) == 31) std::this_thread::yield();
        }

        while (!m_queue.tryPush(std::move(item))) {
            T discard;
            if (m_queue.tryPop(discard)) {
                if (m_config.track_statistics) m_stats.total_dropped.fetch_add(1, std::memory_order_relaxed);
                notifyDrop(discard, "KeepLatest overflow (fallback)");
            }
            std::this_thread::yield();
        }
        updatePeakSize();
        if (m_config.track_statistics) m_stats.total_pushed.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void updatePeakSize() {
        if (!m_config.track_statistics) return;
        size_type current = m_queue.size();
        std::uint64_t peak = m_stats.peak_size.load(std::memory_order_relaxed);
        while (current > peak) {
            if (m_stats.peak_size.compare_exchange_weak(peak, static_cast<std::uint64_t>(current), std::memory_order_relaxed)) {
                break;
            }
        }
    }

    void notifyDrop(const T& dropped_item, const std::string& reason) {
        if (!m_dropCallback) return;

        FrameId frame_id = k_invalid_frame_id;
        if (m_frameIdAccessor) {
            nexusflow::Optional<FrameId> fid_opt = m_frameIdAccessor(dropped_item);
            if (fid_opt.hasValue()) {
                frame_id = fid_opt.value();
            }
        }

        DropEvent event;
        event.frame_id = frame_id;
        event.stream_id = k_default_stream_id;
        event.drop_time = std::chrono::steady_clock::now();
        event.node_name = m_config.node_name;
        event.port_name = m_config.port_name;
        event.reason = reason;
        event.queue_size_before = m_queue.capacity();
        event.queue_size_after = m_queue.size();
        event.total_drops = m_stats.total_dropped.load(std::memory_order_relaxed);

        m_dropCallback(event);
    }

    Config m_config;
    LockFreeQueue<T> m_queue;
    LockFreeQueueStatistics m_stats;

    DropEventCallback m_dropCallback;
    FrameIdAccessor m_frameIdAccessor;
};

} // namespace nexusflow

#endif // LOCK_FREE_QUEUE_HPP
