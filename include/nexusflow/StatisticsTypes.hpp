#ifndef NEXUSFLOW_STATISTICS_TYPES_HPP
#define NEXUSFLOW_STATISTICS_TYPES_HPP

#include <cstdint>
#include <string>

namespace nexusflow {

struct PortStats {
    std::string srcModuleName;
    std::string srcPortName;
    std::string dstModuleName;
    std::string dstPortName;

    std::uint64_t pushAttempts = 0;
    std::uint64_t blockingPushAttempts = 0;
    std::uint64_t nonBlockingPushAttempts = 0;
    std::uint64_t enqueueCount = 0;
    std::uint64_t dropCount = 0;
    std::uint64_t rejectCount = 0;
    std::uint64_t dequeueCount = 0;
    std::uint64_t currentDepth = 0;
    std::uint64_t peakDepth = 0;
};

struct NodeStats {
    std::string nodeName;
    std::uint64_t processCount = 0;
    std::uint64_t inputMessageCount = 0;
    std::uint64_t emittedBroadcastCount = 0;
    std::uint64_t emittedRouteCount = 0;
    std::uint64_t incomingDequeueCount = 0;
    std::uint64_t outgoingEnqueueCount = 0;
    std::uint64_t outgoingDropCount = 0;
    std::uint64_t outgoingRejectCount = 0;
    std::uint64_t pendingJoinGroupCount = 0;
    std::uint64_t joinTimeoutDropCount = 0;
    std::uint64_t joinOverflowDropCount = 0;
};

struct PipelineSummaryStats {
    std::uint64_t totalPushAttempts = 0;
    std::uint64_t totalEnqueueCount = 0;
    std::uint64_t totalDropCount = 0;
    std::uint64_t totalRejectCount = 0;
    std::uint64_t sinkReceiveCount = 0;
    std::uint64_t latencySampleCount = 0;
    std::uint64_t latencyP50Ms = 0;
    std::uint64_t latencyP99Ms = 0;
    std::uint64_t latencyMaxMs = 0;
    double dropRate = 0.0;
    double rejectRate = 0.0;
};

} // namespace nexusflow

#endif // NEXUSFLOW_STATISTICS_TYPES_HPP
