#include <nexusflow/PipelineObserver.hpp>

#include <nexusflow/Pipeline.hpp>

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace nexusflow {

PipelineObserver::PipelineObserver(const Pipeline& pipeline) : m_pipeline(pipeline) {}

PipelineObservation PipelineObserver::Snapshot() const {
    PipelineObservation observation;
    observation.nodes = m_pipeline.GetNodeStats();
    observation.ports = m_pipeline.GetPortStats();

    std::unordered_map<std::string, std::size_t> nodeIndex;
    nodeIndex.reserve(observation.nodes.size());
    for (std::size_t index = 0; index < observation.nodes.size(); ++index) {
        nodeIndex.emplace(observation.nodes[index].nodeName, index);
    }

    for (const auto& port : observation.ports) {
        auto srcIt = nodeIndex.find(port.srcModuleName);
        if (srcIt != nodeIndex.end()) {
            auto& node = observation.nodes[srcIt->second];
            node.outgoingEnqueueCount += port.enqueueCount;
            node.outgoingDropCount += port.dropCount;
            node.outgoingRejectCount += port.rejectCount;
        }

        auto dstIt = nodeIndex.find(port.dstModuleName);
        if (dstIt != nodeIndex.end()) {
            observation.nodes[dstIt->second].incomingDequeueCount += port.dequeueCount;
        }
    }

    std::sort(observation.nodes.begin(), observation.nodes.end(),
              [](const NodeStats& lhs, const NodeStats& rhs) { return lhs.nodeName < rhs.nodeName; });
    std::sort(observation.ports.begin(), observation.ports.end(),
              [](const PortStats& lhs, const PortStats& rhs) {
                  if (lhs.srcModuleName != rhs.srcModuleName) return lhs.srcModuleName < rhs.srcModuleName;
                  if (lhs.srcPortName != rhs.srcPortName) return lhs.srcPortName < rhs.srcPortName;
                  if (lhs.dstModuleName != rhs.dstModuleName) return lhs.dstModuleName < rhs.dstModuleName;
                  return lhs.dstPortName < rhs.dstPortName;
              });

    return observation;
}

std::string PipelineObserver::Describe() const {
    const auto observation = Snapshot();
    std::ostringstream oss;

    oss << "Nodes\n";
    for (const auto& node : observation.nodes) {
        oss << "  " << node.nodeName << ":"
            << " process=" << node.processCount
            << " input=" << node.inputMessageCount
            << " incomingDequeue=" << node.incomingDequeueCount
            << " outgoingEnqueue=" << node.outgoingEnqueueCount
            << " outgoingDrop=" << node.outgoingDropCount
            << " outgoingReject=" << node.outgoingRejectCount
            << " emitBroadcast=" << node.emittedBroadcastCount
            << " emitRoute=" << node.emittedRouteCount
            << " pendingJoins=" << node.pendingJoinGroupCount
            << " joinTimeoutDrop=" << node.joinTimeoutDropCount
            << " joinOverflowDrop=" << node.joinOverflowDropCount << "\n";
    }

    oss << "Ports\n";
    for (const auto& port : observation.ports) {
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
