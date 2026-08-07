#include <nexusflow/PipelineStatistics.hpp>

#include <nexusflow/Pipeline.hpp>

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace nexusflow {

PipelineStatisticsCollector::PipelineStatisticsCollector(const Pipeline& pipeline) : m_pipeline(pipeline) {}

PipelineStatisticsSnapshot PipelineStatisticsCollector::Snapshot() const {
    PipelineStatisticsSnapshot snapshot;
    snapshot.nodes = m_pipeline.GetNodeStats();
    snapshot.ports = m_pipeline.GetPortStats();
    snapshot.summary = m_pipeline.GetSummaryStats();

    std::unordered_map<std::string, std::size_t> nodeIndex;
    nodeIndex.reserve(snapshot.nodes.size());
    for (std::size_t index = 0; index < snapshot.nodes.size(); ++index) {
        nodeIndex.emplace(snapshot.nodes[index].nodeName, index);
    }

    for (const auto& port : snapshot.ports) {
        auto srcIt = nodeIndex.find(port.srcModuleName);
        if (srcIt != nodeIndex.end()) {
            auto& node = snapshot.nodes[srcIt->second];
            node.outgoingEnqueueCount += port.enqueueCount;
            node.outgoingDropCount += port.dropCount;
            node.outgoingRejectCount += port.rejectCount;
        }

        auto dstIt = nodeIndex.find(port.dstModuleName);
        if (dstIt != nodeIndex.end()) {
            snapshot.nodes[dstIt->second].incomingDequeueCount += port.dequeueCount;
        }
    }

    std::sort(snapshot.nodes.begin(), snapshot.nodes.end(),
              [](const NodeStats& lhs, const NodeStats& rhs) { return lhs.nodeName < rhs.nodeName; });
    std::sort(snapshot.ports.begin(), snapshot.ports.end(),
              [](const PortStats& lhs, const PortStats& rhs) {
                  if (lhs.srcModuleName != rhs.srcModuleName) return lhs.srcModuleName < rhs.srcModuleName;
                  if (lhs.srcPortName != rhs.srcPortName) return lhs.srcPortName < rhs.srcPortName;
                  if (lhs.dstModuleName != rhs.dstModuleName) return lhs.dstModuleName < rhs.dstModuleName;
                  return lhs.dstPortName < rhs.dstPortName;
              });

    return snapshot;
}

std::string PipelineStatisticsCollector::Describe() const {
    const auto snapshot = Snapshot();
    std::ostringstream oss;

    oss << "Overview\n";
    oss << "  attempts=" << snapshot.summary.totalPushAttempts
        << " enqueued=" << snapshot.summary.totalEnqueueCount
        << " dropped=" << snapshot.summary.totalDropCount
        << " rejected=" << snapshot.summary.totalRejectCount
        << " sinkCount=" << snapshot.summary.sinkReceiveCount
        << " latencySamples=" << snapshot.summary.latencySampleCount
        << " latencyP50Ms=" << snapshot.summary.latencyP50Ms
        << " latencyP99Ms=" << snapshot.summary.latencyP99Ms
        << " latencyMaxMs=" << snapshot.summary.latencyMaxMs << "\n";

    oss << "Nodes\n";
    for (const auto& node : snapshot.nodes) {
        oss << "  " << node.nodeName << ":"
            << " process=" << node.processCount
            << " input=" << node.inputMessageCount
            << " taskSubmit=" << node.taskSubmitCount
            << " taskRun=" << node.taskRunCount
            << " readySignal=" << node.readySignalCount
            << " reschedule=" << node.rescheduleCount
            << " idleBackoff=" << node.idleBackoffCount
            << " incomingDequeue=" << node.incomingDequeueCount
            << " outgoingEnqueue=" << node.outgoingEnqueueCount
            << " outgoingDrop=" << node.outgoingDropCount
            << " outgoingReject=" << node.outgoingRejectCount
            << " emitBroadcast=" << node.emittedBroadcastCount
            << " emitRoute=" << node.emittedRouteCount
            << " joinInsert=" << node.joinInsertCount
            << " joinComplete=" << node.joinCompleteGroupCount
            << " pendingJoins=" << node.pendingJoinGroupCount
            << " joinTimeoutDrop=" << node.joinTimeoutDropCount
            << " joinOverflowDrop=" << node.joinOverflowDropCount << "\n";
    }

    oss << "Ports\n";
    for (const auto& port : snapshot.ports) {
        oss << "  " << port.srcModuleName << ":" << port.srcPortName << " -> " << port.dstModuleName << ":" << port.dstPortName
            << " enqueue=" << port.enqueueCount
            << " drop=" << port.dropCount
            << " reject=" << port.rejectCount
            << " dequeue=" << port.dequeueCount
            << " depth=" << port.currentDepth
            << " peak=" << port.peakDepth << "\n";
    }

    return oss.str();
}

} // namespace nexusflow
