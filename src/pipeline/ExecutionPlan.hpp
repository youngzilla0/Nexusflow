#ifndef NEXUSFLOW_PIPELINE_EXECUTION_PLAN_HPP
#define NEXUSFLOW_PIPELINE_EXECUTION_PLAN_HPP

#include "base/Graph.hpp"

#include <nexusflow/Module.hpp>
#include <nexusflow/PipelineConfig.hpp>

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow {

/**
 * @brief Graph 到运行时物化之间的内部执行计划。
 *
 * 该结构负责把运行时真正需要的信息提前整理好：
 * - 节点的稳定顺序
 * - 节点索引到模块实例的映射
 * - 节点之间的队列绑定关系
 *
 * 这样 `Pipeline::Impl` 在物化阶段只需要按计划执行，
 * 不必重新扫描图结构或重复推导节点关系。
 */
struct ExecutionPlan {
    using NodeIndex = std::size_t;

    /**
     * @brief 节点级运行时语义快照。
     *
     * 这里保存的是计划生成阶段就已经确定下来的节点运行信息，
     * 后续运行时物化和调度层都应优先消费这份快照，而不是重新回到图层推导。
     */
    struct PlannedNodeRuntimeProfile {
        PipelineConfig runtimeConfig = PipelineConfig::Default();
        bool isSourceNode = false;
        bool isSinkNode = false;
        bool statisticsEnabled = false;
        Module::TriggerPolicy triggerPolicy = Module::TriggerPolicy::Auto;
        Module::SourcePolicy sourcePolicy = Module::SourcePolicy::Polling;
        JoinKeyPolicy joinKeyPolicy = JoinKeyPolicy::MessageId;
        std::size_t idleWaitUs = 0;
        std::size_t fusionTimeoutMs = 0;
        std::size_t maxPendingJoinGroups = 0;
    };

    /**
     * @brief 一条待物化的运行时节点定义。
     */
    struct PlannedModuleNode {
        std::string nodeName;
        std::shared_ptr<GraphNode> graphNode;
        std::shared_ptr<Module> module;
        PlannedNodeRuntimeProfile runtimeProfile;
    };

    /**
     * @brief 一条待物化的队列绑定定义。
     */
    struct PlannedQueueBinding {
        NodeIndex srcNodeIndex = 0;
        NodeIndex dstNodeIndex = 0;
        std::string srcPort;
        std::string dstPort;
        std::size_t queueSize = 0;
        bool statisticsEnabled = false;
    };

    /**
     * @brief 若节点尚未加入计划，则注册一个新节点；否则返回已存在节点索引。
     * @param graphNode 图层节点。
     * @param module 节点对应的模块实例。
     * @return 节点在计划中的稳定索引。
     */
    NodeIndex EnsureNode(const std::shared_ptr<GraphNode>& graphNode,
                         const std::shared_ptr<Module>& module,
                         PlannedNodeRuntimeProfile runtimeProfile) {
        if (!graphNode) {
            throw std::invalid_argument("ExecutionPlan cannot register a null graph node.");
        }
        if (!module) {
            throw std::invalid_argument("ExecutionPlan cannot register a null module.");
        }

        const auto it = nodeIndexByName.find(graphNode->name);
        if (it != nodeIndexByName.end()) {
            return it->second;
        }

        const auto nodeIndex = moduleNodes.size();
        moduleNodes.push_back(PlannedModuleNode{graphNode->name, graphNode, module, std::move(runtimeProfile)});
        nodeIndexByName.emplace(graphNode->name, nodeIndex);
        return nodeIndex;
    }

    /**
     * @brief 追加一条队列绑定关系。
     * @param srcNodeIndex 源节点索引。
     * @param dstNodeIndex 目标节点索引。
     * @param srcPort 源输出端口。
     * @param dstPort 目标输入端口。
     * @param queueSize 队列容量。
     * @param statisticsEnabled 是否为该边启用统计。
     */
    void AddQueueBinding(NodeIndex srcNodeIndex,
                         NodeIndex dstNodeIndex,
                         std::string srcPort,
                         std::string dstPort,
                         std::size_t queueSize,
                         bool statisticsEnabled) {
        queueBindings.push_back(
            PlannedQueueBinding{srcNodeIndex, dstNodeIndex, std::move(srcPort), std::move(dstPort), queueSize, statisticsEnabled});
    }

    /**
     * @brief 根据节点名称查找已注册节点索引。
     * @param nodeName 节点名称。
     * @return 若存在则返回对应索引，否则返回空。
     */
    const NodeIndex* FindNodeIndex(const std::string& nodeName) const {
        const auto it = nodeIndexByName.find(nodeName);
        if (it == nodeIndexByName.end()) {
            return nullptr;
        }
        return &it->second;
    }

    /**
     * @brief 按索引获取一条已注册节点定义。
     * @param nodeIndex 节点索引。
     * @return 对应节点定义。
     */
    const PlannedModuleNode& GetNode(NodeIndex nodeIndex) const {
        if (nodeIndex >= moduleNodes.size()) {
            throw std::out_of_range("ExecutionPlan node index out of range.");
        }
        return moduleNodes[nodeIndex];
    }

    std::vector<PlannedModuleNode> moduleNodes;
    std::vector<PlannedQueueBinding> queueBindings;
    std::unordered_map<std::string, NodeIndex> nodeIndexByName;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_EXECUTION_PLAN_HPP
