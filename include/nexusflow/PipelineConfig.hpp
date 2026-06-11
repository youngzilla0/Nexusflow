#ifndef NEXUSFLOW_PIPELINE_CONFIG_HPP
#define NEXUSFLOW_PIPELINE_CONFIG_HPP

#include <nexusflow/StatisticsOptions.hpp>

#include <cstddef>

namespace nexusflow {

enum class QueueFullPolicy {
    DropTail,
    DropHead,
};

enum class JoinKeyPolicy {
    MessageId,
    Timestamp,
};

struct PipelineConfig {
    size_t executorThreadCount = 0; // 0 = auto
    size_t queueSize = 100;         // Queue capacity between modules
    size_t idleWaitUs = 50;         // Sleep time when no input is available
    size_t fusionTimeoutMs = 60000; // Drop incomplete joins after timeout
    size_t maxPendingJoinGroups = 1024; // Cap pending OnAllInputs join groups, 0 = unlimited
    JoinKeyPolicy joinKeyPolicy = JoinKeyPolicy::MessageId;
    QueueFullPolicy nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;
    StatisticsOptions statistics = StatisticsOptions::Default();

    static PipelineConfig Default() { return PipelineConfig{}; }
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_CONFIG_HPP
