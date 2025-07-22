#ifndef NEXUSFLOW_PROFILER_REGISTRY_HPP
#define NEXUSFLOW_PROFILER_REGISTRY_HPP

#include "Primitives.hpp"
#include <map>
#include <mutex>
#include <string>

namespace nexusflow { namespace profiling {

class ProfilerRegistry {
public:
    ProfilerRegistry(std::string name) : m_name(std::move(name)) {}

    // Methods are now more direct. Key is a simple string, e.g., "module.InputNode.messages_processed".
    Counter& GetCounter(const MetricId& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_counters[name];
    }
    Gauge& GetGauge(const MetricId& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_gauges[name];
    }
    Summary& GetSummary(const MetricId& name) {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_summaries[name];
    }

    // Snapshot methods are now also concrete and easy to use.
    std::map<MetricId, uint64_t> GetCounterValues() const;
    std::map<MetricId, int64_t> GetGaugeValues() const;
    std::map<MetricId, int64_t> GetSummary() const;

private:
    mutable std::mutex m_mutex;
    const std::string m_name;
    std::map<std::string, Counter> m_counters;
    std::map<std::string, Gauge> m_gauges;
    std::map<std::string, Summary> m_summaries;
};

}} // namespace nexusflow::profiling

#endif