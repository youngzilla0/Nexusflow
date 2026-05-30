#include "PipelineImpl.hpp"
#include "base/Graph.hpp"
#include <nexusflow/ModuleFactory.hpp>
#include <stdexcept>

namespace nexusflow {

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
    auto actorNode = std::make_shared<ActorNode>(module, this->config);
    actorModuleMap.emplace(nodeName, actorNode);

    return actorNode;
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

        auto srcActorNode = GetOrCreateActorNode(srcNode);
        auto dstActorNode = GetOrCreateActorNode(dstNode);

        auto queue = std::make_unique<MessageQueue>(this->config.queueSize);
        auto queueView = makeViewPtr(queue.get());

        std::string queueName = srcNode->name + " -> " + dstNode->name;

        srcActorNode->AddOutputQueue(queueName, queueView);
        dstActorNode->AddInputQueue(queueName, queueView);

        queues.push_back(std::move(queue));

        // store ordered actor nodes
        actorOrderedNodes.insert(srcActorNode);
        actorOrderedNodes.insert(dstActorNode);
    }

    CHECK(actorModuleMap.size() == actorOrderedNodes.size(), "actorModuleMap size != actorOrderedNodes size, [{} != {}]",
          actorModuleMap.size(), actorOrderedNodes.size());

    // Apply topology-based join detection
    ApplyTopologyJoin();

    return ErrorCode::SUCCESS;
}

void Pipeline::Impl::ApplyTopologyJoin() {
    if (!graph) return;

    auto convergeNodes = graph->FindConvergeNodes();
    LOG_DEBUG("Topology join analysis: found {} converge nodes", convergeNodes.size());

    for (const auto& node : convergeNodes) {
        // Check if the module explicitly forbid join
        if (auto* nodeWithModule = dynamic_cast<NodeWithModulePtr*>(node.get())) {
            if (nodeWithModule->modulePtr) {
                auto& module = nodeWithModule->modulePtr;
                auto hint = module->GetJoinHint();

                if (hint == Module::JoinHint::NeverJoin) {
                    LOG_DEBUG("Node '{}' is a converge node but JoinHint=NeverJoin, skipping", node->name);
                    continue;
                }

                // Enable join mode (as a hint — actual decision is in JoinInputs())
                module->SetJoinMode(true);
                LOG_DEBUG("Enabled join mode for converge node '{}'", node->name);
            }
        }
    }
}

} // namespace nexusflow
