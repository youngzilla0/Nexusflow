#ifndef NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
#define NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP

#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "core/Worker.hpp"
#include "dispatcher/Dispatcher.hpp"
#include "module/ModuleActor.hpp"
#include "profiling/ProfilerRegistry.hpp"
#include <nexusflow/Pipeline.hpp>
#include <set>
#include <string>

namespace nexusflow {

// Forward declarations
class Pipeline;

// An internal struct to group all runtime components related to a single module.
using ActorName = std::string;
using ActorNode = ModuleActor;

// --- Pipeline's Private Implementation (m_pImpl) ---
class Pipeline::Impl {
public:
    ErrorCode Init(const std::unique_ptr<Graph>& graph);

    void StopQueues();

    // Getter
    const std::vector<MessageQueueUPtr>& GetQueues() const { return m_queues; }
    const std::set<std::shared_ptr<ActorNode>>& GetActorOrderedNodes() const { return m_actorOrderedNodes; }
    const std::unordered_map<ActorName, std::shared_ptr<ActorNode>>& GetActorModuleMap() const { return m_actorModuleMap; }

private:
    std::shared_ptr<ActorNode> GetOrCreateActorNode(const std::shared_ptr</*Graph::*/ Node>& node);

    std::vector<MessageQueueUPtr> m_queues;
    std::set<std::shared_ptr<ActorNode>> m_actorOrderedNodes;
    std::unordered_map<ActorName, std::shared_ptr<ActorNode>> m_actorModuleMap;

    // profiler
    std::unique_ptr<profiling::ProfilerRegistry> m_profilerRegistry;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
