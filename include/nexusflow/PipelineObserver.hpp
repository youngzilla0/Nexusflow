#ifndef NEXUSFLOW_PIPELINE_OBSERVER_HPP
#define NEXUSFLOW_PIPELINE_OBSERVER_HPP

#include <nexusflow/PipelineStatistics.hpp>

namespace nexusflow {

using PipelineObservation = PipelineStatisticsSnapshot;
using PipelineObserver = PipelineStatisticsCollector;

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_OBSERVER_HPP
