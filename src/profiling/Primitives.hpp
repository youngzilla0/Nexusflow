#ifndef NEXUSFLOW_PRIMITIVES_HPP
#define NEXUSFLOW_PRIMITIVES_HPP

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>

namespace nexusflow { namespace profiling {

using MetricId = std::string;

// A simple counter.
class Counter {
public:
    void Increment(int64_t value = 1) { m_value.fetch_add(value, std::memory_order_relaxed); }
    void Decrement(int64_t value = 1) { m_value.fetch_sub(value, std::memory_order_relaxed); }
    int64_t GetValue() const { return m_value.load(std::memory_order_relaxed); }

private:
    std::atomic<int64_t> m_value{0};
};

// A simple gauge.
class Gauge {
public:
    void Set(int64_t value) { m_value.store(value, std::memory_order_relaxed); }
    int64_t GetValue() const { return m_value.load(std::memory_order_relaxed); }

private:
    std::atomic<int64_t> m_value{0};
};

// A simple summary for latency.
class Summary {
public:
    void Observe(double value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_count++;
        m_sum += value;
    }
    // ... GetCount, GetSum, GetAverage ...
private:
    std::mutex m_mutex;
    uint64_t m_count = 0;
    double m_sum = 0.0;
};

// Helper for timing scopes. Uses RAII.
class ScopedTimer {
public:
    explicit ScopedTimer(Summary& summary) : m_summary(summary), m_start(std::chrono::high_resolution_clock::now()) {}

    ~ScopedTimer() {
        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> duration = end - m_start;
        m_summary.Observe(duration.count());
    }

private:
    Summary& m_summary;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
};

}} // namespace nexusflow::profiling
#endif // NEXUSFLOW_PROFILER_METRICS_HPP