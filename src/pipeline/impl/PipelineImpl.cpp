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

} // namespace

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
    // TODO: 使用Any优化一下?
    std::shared_ptr<Module> module;
    if (auto* nodeIns = dynamic_cast<NodeWithModulePtr*>(node.get())) {
        module = nodeIns->modulePtr;
    } else if (auto* nodIns = dynamic_cast<NodeWithModuleClassName*>(node.get())) {
        auto& moduleFactory = ModuleFactory::GetInstance();
        module = moduleFactory.CreateModule(nodIns->moduleClassName, nodIns->name, nodIns->config);
    } else {
        throw std::runtime_error("");
    }

    // 2. 创建 ActiveNode 并存入 map
    auto actorNode = std::make_shared<ActorNode>(module, this->config, pipelineContext, executor);
    actorModuleMap.emplace(nodeName, actorNode);

    return actorNode;
}

ErrorCode Pipeline::Impl::Init() {
    LOG_TRACE("Try init pipeline with graph, [graphName={}]", graph->GetName());

    auto edgeList = graph->toEdgeListBFS();
    LOG_TRACE("edgeList size: {}", edgeList.size());

    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();

        if (!srcNode || !dstNode) {
            LOG_ERROR("An edge contains an expired node pointer. Pipeline initialization failed.");
            throw std::runtime_error("Expired node pointer in graph edge.");
        }

        auto srcActorNode = GetOrCreateActorNode(srcNode);
        auto dstActorNode = GetOrCreateActorNode(dstNode);

        auto queue = std::make_unique<MessageQueue>(this->config.queueSize);
        auto queueView = makeViewPtr(queue.get());
        executor::Executor::PortRuntimeStatsStatePtr portStats;
        if (pipelineContext != nullptr && pipelineContext->IsStatisticsEnabled()) {
            portStats = std::make_shared<executor::Executor::PortRuntimeStatsState>(srcNode->name, edge.srcPort,
                                                                                    dstNode->name, edge.dstPort);
        }

        srcActorNode->AddOutputQueue(edge.srcPort, dstNode->name, edge.dstPort, queueView, portStats);
        dstActorNode->AddInputQueue(edge.dstPort, queueView, portStats);

        queues.push_back(std::move(queue));

        // store ordered actor nodes
        actorOrderedNodes.insert(srcActorNode);
        actorOrderedNodes.insert(dstActorNode);
    }

    CHECK(actorModuleMap.size() == actorOrderedNodes.size(), "actorModuleMap size != actorOrderedNodes size, [{} != {}]",
          actorModuleMap.size(), actorOrderedNodes.size());

    auto executorThreadCount = ResolveExecutorThreadCount(config.executorThreadCount, actorOrderedNodes.size());
    pipelineContext->SetExecutorThreadCount(executorThreadCount);
    executor->SetThreadCount(executorThreadCount);

    ApplyTopologyPolicies();

    return ErrorCode::SUCCESS;
}

void Pipeline::Impl::ApplyTopologyPolicies() {
    if (!graph) return;

    auto convergeNodes = graph->FindConvergeNodes();
    LOG_DEBUG("Topology analysis: found {} converge nodes", convergeNodes.size());
    (void)convergeNodes;
}

} // namespace nexusflow
