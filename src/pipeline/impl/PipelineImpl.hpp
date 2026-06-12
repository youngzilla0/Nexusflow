#ifndef NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
#define NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP

#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "executor/Executor.hpp"
#include "module/ModuleNode.hpp"
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/Pipeline.hpp>
#include <string>

namespace nexusflow {

// Forward declarations
class Pipeline;

struct PipelineBuildPlan {
    /**
     * @brief 一条待物化的运行时边定义。
     */
    struct PlannedEdge {
        std::shared_ptr<GraphNode> srcNode;
        std::shared_ptr<GraphNode> dstNode;
        std::string srcPort;
        std::string dstPort;
    };

    std::vector<std::shared_ptr<GraphNode>> topoNodes;
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

    std::vector<std::shared_ptr<ModuleNode>> moduleNodes;

    ErrorCode Init();

    void ApplyTopologyPolicies();

private:
    ErrorCode ValidateGraph() const;
    PipelineBuildPlan BuildPlan() const;
    ErrorCode MaterializeRuntime(const PipelineBuildPlan& plan);
    std::shared_ptr<ModuleNode> GetOrCreateNode(const std::shared_ptr<GraphNode>& node);

    std::unordered_map<std::string, std::shared_ptr<ModuleNode>> moduleNodeMap;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
