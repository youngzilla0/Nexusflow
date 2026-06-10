#ifndef NEXUSFLOW_PIPELINE_CONFIG_HPP
#define NEXUSFLOW_PIPELINE_CONFIG_HPP

#include <cstddef>

namespace nexusflow {

enum class QueueFullPolicy {
    DropTail,
    DropHead,
};

struct PipelineConfig {
    size_t executorThreadCount = 0; // 0 = auto
    size_t queueSize = 100;         // Queue capacity between modules
    size_t idleWaitUs = 50;         // Sleep time when no input is available
    size_t fusionTimeoutMs = 60000; // Drop incomplete joins after timeout
    size_t maxPendingJoinGroups = 1024; // Cap pending OnAllInputs join groups, 0 = unlimited
    QueueFullPolicy nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    static PipelineConfig Default() { return PipelineConfig{}; }
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_CONFIG_HPP
