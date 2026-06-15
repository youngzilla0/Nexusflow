#ifndef NEXUSFLOW_PIPELINE_HPP
#define NEXUSFLOW_PIPELINE_HPP

#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/PipelineEvents.hpp>
#include <nexusflow/PipelineConfig.hpp>
#include <nexusflow/StatisticsTypes.hpp>

#include <memory>
#include <string>
#include <vector>

// Forward declaration
class Graph;
namespace nexusflow {
class PipelineBuilder;
}

namespace nexusflow {

/**
 * @brief Pipeline
 *
 */
class Pipeline {
public:
    static std::unique_ptr<Pipeline> CreateFromYaml(const std::string& configPath);

    ErrorCode Init();

    ErrorCode Start();

    ErrorCode Stop();

    ErrorCode DeInit();

    void AddObserver(const std::shared_ptr<IPipelineObserver>& observer);

    void RemoveObserver(const std::shared_ptr<IPipelineObserver>& observer);

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
