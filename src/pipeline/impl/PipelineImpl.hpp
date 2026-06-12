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

/**
 * @brief Pipeline 内部使用的模块运行时节点名称。
 */
using ActorName = std::string;

/**
 * @brief Pipeline 生命周期列表中使用的模块运行时包装节点类型。
 *
 * 这里的 ActorNode 实际上是 ModuleActor 的别名，
 * 表示“Pipeline 视角下的运行时模块节点”。
 */
using ActorNode = ModuleActor;

struct PipelineBuildPlan {
    /**
     * @brief 一条待物化的运行时边定义。
     */
    struct PlannedEdge {
        std::shared_ptr<Node> srcNode;
        std::shared_ptr<Node> dstNode;
        std::string srcPort;
        std::string dstPort;
    };

    std::vector<std::shared_ptr<Node>> topoNodes;
    std::vector<PlannedEdge> edges;
};

/**
 * @brief Pipeline 的私有实现。
 */
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
