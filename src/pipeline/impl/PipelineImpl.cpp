#include "PipelineImpl.hpp"
#include "base/Graph.hpp"
#include <nexusflow/ModuleFactory.hpp>
#include <stdexcept>

namespace nexusflow {

std::shared_ptr<ActiveNode> Pipeline::Impl::GetOrCreateActiveNode(const std::shared_ptr<Node>& node) {
    const auto& nodeName = node->name;

    // 检查 activeNodeMap 中是否已存在
    auto it = activeNodeMap.find(nodeName);
    if (it != activeNodeMap.end()) {
        return it->second; // 已存在，直接返回
    }

    // 不存在，则创建新的 ActiveNode
    // 1. 获取或创建 Module
    // TODO: 使用Variant优化一下?
    std::shared_ptr<Module> module;
    if (auto* nodeIns = dynamic_cast<NodeWithModulePtr*>(node.get())) {
        module = nodeIns->modulePtr;
    } else if (auto* nodIns = dynamic_cast<NodeWithModuleClassName*>(node.get())) {
        auto& moduleFactory = ModuleFactory::GetInstance();
        module = moduleFactory.CreateModule(nodIns->moduleClassName, nodIns->name, nodIns->paramMap);
    } else {
        throw std::runtime_error("");
    }

    // 2. 创建 ActiveNode 并存入 map
    auto activeNode = std::make_shared<ActiveNode>(module);
    activeNodeMap.emplace(nodeName, activeNode);

    return activeNode;
}

ErrorCode Pipeline::Impl::Init() {
    LOG_TRACE("Try init pipeline with graph, [graphName={}]", graph->getName());

    auto edgeList = graph->toEdgeListBFS();
    LOG_TRACE("edgeList size: {}", edgeList.size());

    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();

        if (!srcNode || !dstNode) {
            LOG_ERROR("An edge contains an expired node pointer. Pipeline initialization failed.");
            throw std::runtime_error("Expired node pointer in graph edge.");
        }

        auto srcActiveNode = GetOrCreateActiveNode(srcNode);
        auto dstActiveNode = GetOrCreateActiveNode(dstNode);

        constexpr int kQueueSize = 5;
        auto queue = std::make_unique<MessageQueue>(kQueueSize);
        auto queueView = makeViewPtr(queue.get());

        std::string queueName = srcNode->name + " -> " + dstNode->name;

        srcActiveNode->AddOutputQueue(queueName, queueView);
        dstActiveNode->AddInputQueue(queueName, queueView);

        queues.push_back(std::move(queue));
    }
    return ErrorCode::SUCCESS;
}

} // namespace nexusflow
