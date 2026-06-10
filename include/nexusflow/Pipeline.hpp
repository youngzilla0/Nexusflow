#ifndef NEXUSFLOW_PIPELINE_HPP
#define NEXUSFLOW_PIPELINE_HPP

#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/PipelineConfig.hpp>
#include <nexusflow/RuntimeStats.hpp>

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

    std::vector<PortRuntimeStats> GetPortStats() const;
    std::vector<ActorRuntimeStats> GetActorStats() const;

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
