#ifndef NEXUSFLOW_PIPELINE_OBSERVER_HPP
#define NEXUSFLOW_PIPELINE_OBSERVER_HPP

#include <nexusflow/StatisticsTypes.hpp>

#include <string>
#include <vector>

namespace nexusflow {

class Pipeline;

struct PipelineObservation {
    std::vector<NodeStats> nodes;
    std::vector<PortStats> ports;
};

class PipelineObserver {
public:
    explicit PipelineObserver(const Pipeline& pipeline);

    PipelineObservation Snapshot() const;
    std::string Describe() const;

private:
    const Pipeline& m_pipeline;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_OBSERVER_HPP
