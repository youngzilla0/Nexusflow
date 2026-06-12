#ifndef NEXUSFLOW_EXECUTOR_NODE_REGISTRY_HPP
#define NEXUSFLOW_EXECUTOR_NODE_REGISTRY_HPP

#include "JoinStateStore.hpp"
#include "Statistics.hpp"
#include "common/ViewPtr.hpp"

#include <nexusflow/Module.hpp>
#include <nexusflow/PipelineConfig.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

/**
 * @brief 维护 Executor 运行期的节点状态注册表。
 *
 * NodeRegistry 只负责：
 * - 节点状态的创建与查找
 * - 输入队列绑定
 * - 启动前的状态快照
 * - 节点级统计状态注册
 *
 * 它不参与调度策略、线程池执行和端口分发。
 */
class NodeRegistry {
public:
    using PortStatsStatePtr = Statistics::PortStatsStatePtr;
    using NodeStatsState = Statistics::NodeStatsState;
    using NodeStatsStatePtr = Statistics::NodeStatsStatePtr;

    /**
     * @brief 节点输入端口与底层队列的绑定关系。
     */
    struct InputQueueBinding {
        std::string inputPortName;
        ViewPtr<MessageQueue> queue;
        PortStatsStatePtr stats;
    };

    /**
     * @brief 单个节点在 Executor 调度层中的运行时状态。
     */
    struct NodeState {
        std::string nodeName;
        std::shared_ptr<Module> module;
        PipelineConfig runtimeConfig;
        std::vector<InputQueueBinding> inputQueues;
        std::size_t nextInputIndex = 0; // OnAnyInput 下用于轮转扫描输入队列，避免总是偏向第一个端口。
        JoinStateStore joinState;       // 只在 OnAllInputs 下使用，缓存待拼齐的 join group。
        std::atomic<bool> taskScheduled{false}; // 当前节点是否已在池中排队或执行，避免重复提交。
        std::atomic<std::uint64_t> pendingRunSignals{0}; // 记录执行期间新增的“需要再跑一次”信号。
        NodeStatsStatePtr stats = std::make_shared<NodeStatsState>();
    };

    using NodeStatePtr = std::shared_ptr<NodeState>;

    /**
     * @brief 注册一个可调度节点。
     * @param nodeName 节点名称。
     * @param module 对应的模块实例。
     * @param runtimeConfig 节点运行时配置。
     * @param statistics 统计聚合器，用于登记节点统计状态。
     */
    void RegisterNode(const std::string& nodeName,
                      const std::shared_ptr<Module>& module,
                      const PipelineConfig& runtimeConfig,
                      Statistics& statistics);

    /**
     * @brief 为指定节点添加一个输入队列绑定。
     * @param nodeName 目标节点名称。
     * @param inputPortName 输入端口名称。
     * @param queue 输入消息队列。
     * @param stats 对应边的统计状态。
     */
    void AddInputQueue(const std::string& nodeName,
                       const std::string& inputPortName,
                       ViewPtr<MessageQueue> queue,
                       const PortStatsStatePtr& stats);

    /**
     * @brief 查找指定节点的运行时状态。
     * @param nodeName 节点名称。
     * @return 对应节点状态；若不存在则返回空指针。
     */
    NodeStatePtr Find(const std::string& nodeName) const;

    /**
     * @brief 返回当前全部节点状态的稳定快照。
     * @return 节点状态快照列表。
     */
    std::vector<NodeStatePtr> SnapshotStates() const;

    /**
     * @brief 返回当前已注册的节点数量。
     * @return 节点数量。
     */
    std::size_t Size() const;

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, NodeStatePtr> m_nodeStates;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_NODE_REGISTRY_HPP
