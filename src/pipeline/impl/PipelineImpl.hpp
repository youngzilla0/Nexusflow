#ifndef NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
#define NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP

#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "executor/Executor.hpp"
#include "module/ModuleActor.hpp"
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/Pipeline.hpp>
#include <string>

namespace nexusflow {

// Forward declarations
class Pipeline;

// An internal struct to group all runtime components related to a single module.
using ActorName = std::string;
using ActorNode = ModuleActor;

struct PipelineBuildPlan {
    struct PlannedEdge {
        std::shared_ptr<Node> srcNode;
        std::shared_ptr<Node> dstNode;
        std::string srcPort;
        std::string dstPort;
    };

    std::vector<std::shared_ptr<Node>> topoNodes;
    std::vector<PlannedEdge> edges;
};

// --- Pipeline's Private Implementation (m_pImpl) ---
class Pipeline::Impl {
public:
    std::unique_ptr<Graph> graph;
    std::vector<MessageQueueUPtr> queues;
    PipelineConfig config;
    std::shared_ptr<PipelineContext> pipelineContext;
    std::shared_ptr<executor::Executor> executor;

    std::vector<std::shared_ptr<ActorNode>> actorOrderedNodes;

    ErrorCode Init();

    void ApplyTopologyPolicies();

private:
    ErrorCode ValidateGraph() const;
    PipelineBuildPlan BuildPlan() const;
    ErrorCode MaterializeRuntime(const PipelineBuildPlan& plan);
    std::shared_ptr<ActorNode> GetOrCreateActorNode(const std::shared_ptr</*Graph::*/ Node>& node);

    std::unordered_map<ActorName, std::shared_ptr<ActorNode>> actorModuleMap;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
