#include <nexusflow/PipelineObserver.hpp>

#include <nexusflow/Pipeline.hpp>

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace nexusflow {

PipelineObserver::PipelineObserver(const Pipeline& pipeline) : m_pipeline(pipeline) {}

PipelineObservation PipelineObserver::Snapshot() const {
    PipelineObservation observation;
    observation.actors = m_pipeline.GetActorStats();
    observation.ports = m_pipeline.GetPortStats();

    std::unordered_map<std::string, std::size_t> actorIndex;
    actorIndex.reserve(observation.actors.size());
    for (std::size_t index = 0; index < observation.actors.size(); ++index) {
        actorIndex.emplace(observation.actors[index].actorName, index);
    }

    for (const auto& port : observation.ports) {
        auto srcIt = actorIndex.find(port.srcModuleName);
        if (srcIt != actorIndex.end()) {
            auto& actor = observation.actors[srcIt->second];
            actor.outgoingEnqueueCount += port.enqueueCount;
            actor.outgoingDropCount += port.dropCount;
            actor.outgoingRejectCount += port.rejectCount;
        }

        auto dstIt = actorIndex.find(port.dstModuleName);
        if (dstIt != actorIndex.end()) {
            observation.actors[dstIt->second].incomingDequeueCount += port.dequeueCount;
        }
    }

    std::sort(observation.actors.begin(), observation.actors.end(),
              [](const ActorRuntimeStats& lhs, const ActorRuntimeStats& rhs) { return lhs.actorName < rhs.actorName; });
    std::sort(observation.ports.begin(), observation.ports.end(),
              [](const PortRuntimeStats& lhs, const PortRuntimeStats& rhs) {
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

    oss << "Actors\n";
    for (const auto& actor : observation.actors) {
        oss << "  " << actor.actorName << ":"
            << " process=" << actor.processCount
            << " input=" << actor.inputMessageCount
            << " incomingDequeue=" << actor.incomingDequeueCount
            << " outgoingEnqueue=" << actor.outgoingEnqueueCount
            << " outgoingDrop=" << actor.outgoingDropCount
            << " outgoingReject=" << actor.outgoingRejectCount
            << " emitBroadcast=" << actor.emittedBroadcastCount
            << " emitRoute=" << actor.emittedRouteCount
            << " pendingJoins=" << actor.pendingJoinGroupCount
            << " joinTimeoutDrop=" << actor.joinTimeoutDropCount
            << " joinOverflowDrop=" << actor.joinOverflowDropCount << "\n";
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
