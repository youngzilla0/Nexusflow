#include "PipelineImpl.hpp"
#include "base/Graph.hpp"
#include "utils/logging.hpp"
#include <nexusflow/ModuleFactory.hpp>

#include <algorithm>
#include <stdexcept>
#include <thread>

namespace nexusflow {

namespace {

std::size_t ResolveExecutorThreadCount(std::size_t configuredThreadCount, std::size_t actorCount) {
    (void)actorCount;
    if (configuredThreadCount > 0) {
        return configuredThreadCount;
    }

    auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    if (hardwareThreads == 0) {
        hardwareThreads = 1;
    }
    return std::max<std::size_t>(hardwareThreads, 1);
}

std::shared_ptr<Module> CreateModuleForGraphNode(const std::shared_ptr<GraphNode>& node) {
    if (!node) {
        throw std::runtime_error("Cannot create a module from a null graph node.");
    }

    switch (node->GetKind()) {
        case GraphNodeKind::Module: {
            auto& graphModuleNode = static_cast<GraphModuleNode&>(*node);
            if (graphModuleNode.source == ModuleSource::Instance) {
                return graphModuleNode.modulePtr;
            }

            auto& moduleFactory = ModuleFactory::GetInstance();
            return moduleFactory.CreateModule(
                ModuleBuildContext{graphModuleNode.moduleClassName, graphModuleNode.name, graphModuleNode.config});
        }
        case GraphNodeKind::Generic:
        default:
            throw std::runtime_error("Graph node '" + node->name +
                                     "' only describes topology and cannot materialize a Module.");
    }
}

} // namespace

ErrorCode Pipeline::Impl::ValidateGraph() const {
    if (!graph) {
        LOG_ERROR("Pipeline graph is null.");
        return ErrorCode::UNINITIALIZED_ERROR;
    }

    if (graph->GetName().empty()) {
        LOG_ERROR("Pipeline graph name is empty.");
        return ErrorCode::FAILURE;
    }

    if (graph->IsEmpty()) {
        LOG_ERROR("Pipeline graph '{}' is empty or incomplete.", graph->GetName());
        return ErrorCode::FAILURE;
    }

    if (graph->HasCycle()) {
        LOG_ERROR("Pipeline graph '{}' contains a cycle.", graph->GetName());
        return ErrorCode::FAILURE;
    }

    return ErrorCode::SUCCESS;
}

ExecutionPlan Pipeline::Impl::BuildExecutionPlan() const {
    ExecutionPlan plan;
    auto edgeList = graph->ToEdgeListBfs();
    LOG_TRACE("edgeList size: {}", edgeList.size());

    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();

        if (!srcNode || !dstNode) {
            throw std::runtime_error("Expired node pointer in graph edge.");
        }

        const auto* existingSrcNodeIndex = plan.FindNodeIndex(srcNode->name);
        const auto srcNodeIndex = existingSrcNodeIndex != nullptr ? *existingSrcNodeIndex
                                                                  : plan.EnsureNode(srcNode, CreateModuleForGraphNode(srcNode));
        const auto* existingDstNodeIndex = plan.FindNodeIndex(dstNode->name);
        const auto dstNodeIndex = existingDstNodeIndex != nullptr ? *existingDstNodeIndex
                                                                  : plan.EnsureNode(dstNode, CreateModuleForGraphNode(dstNode));
        plan.AddQueueBinding(srcNodeIndex,
                             dstNodeIndex,
                             edge.srcPort,
                             edge.dstPort,
                             config.queueSize,
                             pipelineContext != nullptr && pipelineContext->IsStatisticsEnabled());
    }

    return plan;
}

ErrorCode Pipeline::Impl::MaterializeRuntime(const ExecutionPlan& plan) {
    moduleNodes.clear();
    moduleNodes.reserve(plan.moduleNodes.size());

    for (const auto& plannedNode : plan.moduleNodes) {
        moduleNodes.push_back(std::make_shared<ModuleNode>(plannedNode.module, this->config, pipelineContext, executor));
    }

    for (const auto& queueBinding : plan.queueBindings) {
        if (queueBinding.srcNodeIndex >= moduleNodes.size() || queueBinding.dstNodeIndex >= moduleNodes.size()) {
            throw std::runtime_error("Execution plan contains an invalid node index in queue bindings.");
        }

        const auto& srcPlannedNode = plan.GetNode(queueBinding.srcNodeIndex);
        const auto& dstPlannedNode = plan.GetNode(queueBinding.dstNodeIndex);
        auto& srcModuleNode = moduleNodes[queueBinding.srcNodeIndex];
        auto& dstModuleNode = moduleNodes[queueBinding.dstNodeIndex];

        auto queue = std::make_unique<MessageQueue>(queueBinding.queueSize);
        auto queueView = makeViewPtr(queue.get());
        executor::Executor::PortStatsStatePtr portStats;
        if (queueBinding.statisticsEnabled) {
            portStats = std::make_shared<executor::Executor::PortStatsState>(
                srcPlannedNode.nodeName, queueBinding.srcPort, dstPlannedNode.nodeName, queueBinding.dstPort);
        }

        srcModuleNode->AddOutputQueue(queueBinding.srcPort, dstPlannedNode.nodeName, queueBinding.dstPort, queueView, portStats);
        dstModuleNode->AddInputQueue(queueBinding.dstPort, queueView, portStats);

        queues.push_back(std::move(queue));
    }

    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::Impl::Init() {
    LOG_TRACE("Try init pipeline with graph, [graphName={}]", graph->GetName());
    auto validationResult = ValidateGraph();
    if (validationResult != ErrorCode::SUCCESS) {
        return validationResult;
    }

    auto plan = BuildExecutionPlan();
    auto materializeResult = MaterializeRuntime(plan);
    if (materializeResult != ErrorCode::SUCCESS) {
        return materializeResult;
    }

    auto executorThreadCount = ResolveExecutorThreadCount(config.executorThreadCount, moduleNodes.size());
    pipelineContext->SetExecutorThreadCount(executorThreadCount);
    executor->SetThreadCount(executorThreadCount);

    ApplyTopologyPolicies();

    return ErrorCode::SUCCESS;
}

void Pipeline::Impl::ApplyTopologyPolicies() {
    if (!graph) return;

    auto convergeNodes = graph->GetConvergeNodes();
    LOG_DEBUG("Topology analysis: found {} converge nodes", convergeNodes.size());
    (void)convergeNodes;
}

} // namespace nexusflow
