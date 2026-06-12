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

std::shared_ptr<Module> CreateModuleForGraphNode(const std::shared_ptr<Node>& node) {
    if (!node) {
        throw std::runtime_error("Cannot create a module from a null graph node.");
    }

    switch (node->GetKind()) {
        case NodeKind::Module: {
            auto& moduleNode = static_cast<ModuleNode&>(*node);
            if (moduleNode.source == ModuleSource::Instance) {
                return moduleNode.modulePtr;
            }

            auto& moduleFactory = ModuleFactory::GetInstance();
            return moduleFactory.CreateModule(
                ModuleBuildContext{moduleNode.moduleClassName, moduleNode.name, moduleNode.config});
        }
        case NodeKind::Generic:
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

PipelineBuildPlan Pipeline::Impl::BuildPlan() const {
    PipelineBuildPlan plan;
    auto edgeList = graph->ToEdgeListBfs();
    LOG_TRACE("edgeList size: {}", edgeList.size());

    std::unordered_map<std::string, std::shared_ptr<Node>> orderedNodes;
    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();

        if (!srcNode || !dstNode) {
            throw std::runtime_error("Expired node pointer in graph edge.");
        }

        if (orderedNodes.find(srcNode->name) == orderedNodes.end()) {
            orderedNodes.emplace(srcNode->name, srcNode);
            plan.topoNodes.push_back(srcNode);
        }
        if (orderedNodes.find(dstNode->name) == orderedNodes.end()) {
            orderedNodes.emplace(dstNode->name, dstNode);
            plan.topoNodes.push_back(dstNode);
        }

        plan.edges.push_back(PipelineBuildPlan::PlannedEdge{srcNode, dstNode, edge.srcPort, edge.dstPort});
    }

    return plan;
}

ErrorCode Pipeline::Impl::MaterializeRuntime(const PipelineBuildPlan& plan) {
    for (const auto& plannedEdge : plan.edges) {
        auto srcActorNode = GetOrCreateActorNode(plannedEdge.srcNode);
        auto dstActorNode = GetOrCreateActorNode(plannedEdge.dstNode);

        auto queue = std::make_unique<MessageQueue>(this->config.queueSize);
        auto queueView = makeViewPtr(queue.get());
        executor::Executor::PortStatsStatePtr portStats;
        if (pipelineContext != nullptr && pipelineContext->IsStatisticsEnabled()) {
            portStats = std::make_shared<executor::Executor::PortStatsState>(
                plannedEdge.srcNode->name, plannedEdge.srcPort, plannedEdge.dstNode->name, plannedEdge.dstPort);
        }

        srcActorNode->AddOutputQueue(plannedEdge.srcPort, plannedEdge.dstNode->name, plannedEdge.dstPort, queueView, portStats);
        dstActorNode->AddInputQueue(plannedEdge.dstPort, queueView, portStats);

        queues.push_back(std::move(queue));
    }

    actorOrderedNodes.clear();
    actorOrderedNodes.reserve(plan.topoNodes.size());
    for (const auto& node : plan.topoNodes) {
        auto it = actorModuleMap.find(node->name);
        if (it != actorModuleMap.end()) {
            actorOrderedNodes.push_back(it->second);
        }
    }

    CHECK(actorModuleMap.size() == actorOrderedNodes.size(), "actorModuleMap size != actorOrderedNodes size, [{} != {}]",
          actorModuleMap.size(), actorOrderedNodes.size());

    return ErrorCode::SUCCESS;
}

std::shared_ptr<ActorNode> Pipeline::Impl::GetOrCreateActorNode(const std::shared_ptr<Node>& node) {
    // 此处NodeName == ModuleName
    const auto& nodeName = node->name;

    // 检查 activeNodeMap 中是否已存在
    auto it = actorModuleMap.find(nodeName);
    if (it != actorModuleMap.end()) {
        return it->second; // 已存在，直接返回
    }

    // 不存在，则创建新的 ActiveNode
    // 1. 获取或创建 Module
    auto module = CreateModuleForGraphNode(node);

    // 2. 创建 ActiveNode 并存入 map
    auto actorNode = std::make_shared<ActorNode>(module, this->config, pipelineContext, executor);
    actorModuleMap.emplace(nodeName, actorNode);

    return actorNode;
}

ErrorCode Pipeline::Impl::Init() {
    LOG_TRACE("Try init pipeline with graph, [graphName={}]", graph->GetName());
    auto validationResult = ValidateGraph();
    if (validationResult != ErrorCode::SUCCESS) {
        return validationResult;
    }

    auto plan = BuildPlan();
    auto materializeResult = MaterializeRuntime(plan);
    if (materializeResult != ErrorCode::SUCCESS) {
        return materializeResult;
    }

    auto executorThreadCount = ResolveExecutorThreadCount(config.executorThreadCount, actorOrderedNodes.size());
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
