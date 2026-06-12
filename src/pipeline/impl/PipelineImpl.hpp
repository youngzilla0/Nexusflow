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

struct ExecutionPlan {
    /**
     * @brief 一条待物化的运行时节点定义。
     */
    struct PlannedModuleNode {
        std::shared_ptr<GraphNode> graphNode;
        std::shared_ptr<Module> module;
        Module::TriggerPolicy triggerPolicy = Module::TriggerPolicy::Auto;
        Module::SourcePolicy sourcePolicy = Module::SourcePolicy::Polling;
    };

    /**
     * @brief 一条待物化的运行时边定义。
     */
    struct PlannedEdge {
        std::shared_ptr<GraphNode> srcNode;
        std::shared_ptr<GraphNode> dstNode;
        std::string srcPort;
        std::string dstPort;
        std::size_t queueSize = 0;
        bool statisticsEnabled = false;
    };

    std::vector<PlannedModuleNode> moduleNodes;
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
    ExecutionPlan BuildExecutionPlan() const;
    ErrorCode MaterializeRuntime(const ExecutionPlan& plan);
    std::shared_ptr<ModuleNode> GetOrCreateNode(const ExecutionPlan::PlannedModuleNode& plannedNode);

    std::unordered_map<std::string, std::shared_ptr<ModuleNode>> moduleNodeMap;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
