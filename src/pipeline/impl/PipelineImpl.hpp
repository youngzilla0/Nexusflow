#ifndef NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
#define NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP

#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "executor/Executor.hpp"
#include "module/ModuleNode.hpp"
#include "pipeline/ExecutionPlan.hpp"
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/Pipeline.hpp>
#include <string>

namespace nexusflow {

// Forward declarations
class Pipeline;

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
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
