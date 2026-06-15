#ifndef NEXUSFLOW_PIPELINE_HPP
#define NEXUSFLOW_PIPELINE_HPP

#include <nexusflow/Error.hpp>
#include <nexusflow/PipelineEvents.hpp>
#include <nexusflow/PipelineConfig.hpp>
#include <nexusflow/TopologyTypes.hpp>
#include <nexusflow/StatisticsTypes.hpp>

#include <memory>
#include <string>
#include <vector>

class Graph;

namespace nexusflow {
class PipelineBuilder;

/**
 * @brief Pipeline
 *
 */
class Pipeline {
public:
    static std::unique_ptr<Pipeline> CreateFromYaml(const std::string& configPath);

    Error Init();

    Error Start();

    Error Stop();

    Error DeInit();

    void AddObserver(const std::shared_ptr<IPipelineObserver>& observer);

    void RemoveObserver(const std::shared_ptr<IPipelineObserver>& observer);

    GraphTopologyInfo GetTopologyInfo() const;

    std::vector<PortStats> GetPortStats() const;
    std::vector<NodeStats> GetNodeStats() const;
    PipelineSummaryStats GetSummaryStats() const;

    ~Pipeline();

private:
    friend PipelineBuilder;

    Pipeline();

    void InitWithGraph(std::unique_ptr<Graph> graph, const PipelineConfig& config);

    class Impl;
    std::unique_ptr<Impl> m_pImpl;
};

} // namespace nexusflow

#endif
