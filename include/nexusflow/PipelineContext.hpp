#ifndef NEXUSFLOW_PIPELINE_CONTEXT_HPP
#define NEXUSFLOW_PIPELINE_CONTEXT_HPP

#include <nexusflow/PipelineConfig.hpp>
#include <nexusflow/TopologyTypes.hpp>
#include <nexusflow/StatisticsOptions.hpp>

#include <cstddef>
#include <string>
#include <utility>

namespace nexusflow {

namespace executor {
class Executor;
}

class ModuleNode;
class Pipeline;

class PipelineContext {
public:
    const std::string& GetPipelineName() const { return m_pipelineName; }

    const PipelineConfig& GetConfig() const { return m_config; }

    const StatisticsOptions& GetStatisticsOptions() const { return m_statisticsOptions; }

    const GraphTopologyInfo& GetTopologyInfo() const { return m_topologyInfo; }

    bool IsStatisticsEnabled() const { return m_statisticsOptions.enableStatistics; }

    bool IsThroughputStatisticsEnabled() const {
        return m_statisticsOptions.enableStatistics && m_statisticsOptions.enableThroughput;
    }

    bool IsLatencyStatisticsEnabled() const {
        return m_statisticsOptions.enableStatistics && m_statisticsOptions.enableLatency;
    }

    size_t GetExecutorThreadCount() const { return m_executorThreadCount; }

    void SetExecutorThreadCount(size_t threadCount) { m_executorThreadCount = threadCount; }

    void SetStatisticsEnabled(bool enabled) {
        m_statisticsOptions.enableStatistics = enabled;
        m_config.statistics.enableStatistics = enabled;
    }

    void SetStatisticsOptions(const StatisticsOptions& statisticsOptions) {
        m_statisticsOptions = statisticsOptions;
        m_config.statistics = statisticsOptions;
    }

    void SetTopologyInfo(GraphTopologyInfo topologyInfo) { m_topologyInfo = std::move(topologyInfo); }

private:
    friend class ModuleNode;
    friend class Pipeline;
    friend class executor::Executor;

    PipelineContext(std::string pipelineName, PipelineConfig config)
        : m_pipelineName(std::move(pipelineName)),
          m_config(std::move(config)),
          m_statisticsOptions(m_config.statistics) {}

    std::string m_pipelineName;
    PipelineConfig m_config;
    StatisticsOptions m_statisticsOptions;
    GraphTopologyInfo m_topologyInfo;
    size_t m_executorThreadCount = 0;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_CONTEXT_HPP
