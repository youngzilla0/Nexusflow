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

    std::unordered_map<std::string, std::shared_ptr<GraphNode>> orderedNodes;
    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();

        if (!srcNode || !dstNode) {
            throw std::runtime_error("Expired node pointer in graph edge.");
        }

        if (orderedNodes.find(srcNode->name) == orderedNodes.end()) {
            orderedNodes.emplace(srcNode->name, srcNode);
            auto module = CreateModuleForGraphNode(srcNode);
            plan.moduleNodes.push_back(
                ExecutionPlan::PlannedModuleNode{srcNode, module, module->GetTriggerPolicy(), module->GetSourcePolicy()});
        }
        if (orderedNodes.find(dstNode->name) == orderedNodes.end()) {
            orderedNodes.emplace(dstNode->name, dstNode);
            auto module = CreateModuleForGraphNode(dstNode);
            plan.moduleNodes.push_back(
                ExecutionPlan::PlannedModuleNode{dstNode, module, module->GetTriggerPolicy(), module->GetSourcePolicy()});
        }

        plan.edges.push_back(
            ExecutionPlan::PlannedEdge{srcNode,
                                       dstNode,
                                       edge.srcPort,
                                       edge.dstPort,
                                       config.queueSize,
                                       pipelineContext != nullptr && pipelineContext->IsStatisticsEnabled()});
    }

    return plan;
}

ErrorCode Pipeline::Impl::MaterializeRuntime(const ExecutionPlan& plan) {
    for (const auto& plannedNode : plan.moduleNodes) {
        GetOrCreateNode(plannedNode);
    }

    for (const auto& plannedEdge : plan.edges) {
        auto srcIt = std::find_if(plan.moduleNodes.begin(), plan.moduleNodes.end(),
                                  [&](const ExecutionPlan::PlannedModuleNode& plannedNode) {
                                      return plannedNode.graphNode == plannedEdge.srcNode;
                                  });
        auto dstIt = std::find_if(plan.moduleNodes.begin(), plan.moduleNodes.end(),
                                  [&](const ExecutionPlan::PlannedModuleNode& plannedNode) {
                                      return plannedNode.graphNode == plannedEdge.dstNode;
                                  });
        if (srcIt == plan.moduleNodes.end() || dstIt == plan.moduleNodes.end()) {
            throw std::runtime_error("Execution plan references an unknown module node.");
        }

        auto srcModuleNode = GetOrCreateNode(*srcIt);
        auto dstModuleNode = GetOrCreateNode(*dstIt);

        auto queue = std::make_unique<MessageQueue>(plannedEdge.queueSize);
        auto queueView = makeViewPtr(queue.get());
        executor::Executor::PortStatsStatePtr portStats;
        if (plannedEdge.statisticsEnabled) {
            portStats = std::make_shared<executor::Executor::PortStatsState>(
                plannedEdge.srcNode->name, plannedEdge.srcPort, plannedEdge.dstNode->name, plannedEdge.dstPort);
        }

        srcModuleNode->AddOutputQueue(plannedEdge.srcPort, plannedEdge.dstNode->name, plannedEdge.dstPort, queueView, portStats);
        dstModuleNode->AddInputQueue(plannedEdge.dstPort, queueView, portStats);

        queues.push_back(std::move(queue));
    }

    moduleNodes.clear();
    moduleNodes.reserve(plan.moduleNodes.size());
    for (const auto& plannedNode : plan.moduleNodes) {
        auto it = moduleNodeMap.find(plannedNode.graphNode->name);
        if (it != moduleNodeMap.end()) {
            moduleNodes.push_back(it->second);
        }
    }

    CHECK(moduleNodeMap.size() == moduleNodes.size(), "moduleNodeMap size != moduleNodes size, [{} != {}]", moduleNodeMap.size(),
          moduleNodes.size());

    return ErrorCode::SUCCESS;
}

std::shared_ptr<ModuleNode> Pipeline::Impl::GetOrCreateNode(const ExecutionPlan::PlannedModuleNode& plannedNode) {
    // 此处 graph node name == module name
    const auto& nodeName = plannedNode.graphNode->name;

    // 检查缓存中是否已存在
    auto it = moduleNodeMap.find(nodeName);
    if (it != moduleNodeMap.end()) {
        return it->second; // 已存在，直接返回
    }

    // 不存在，则创建新的运行时包装节点
    auto moduleNode = std::make_shared<ModuleNode>(plannedNode.module, this->config, pipelineContext, executor);
    moduleNodeMap.emplace(nodeName, moduleNode);

    return moduleNode;
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
