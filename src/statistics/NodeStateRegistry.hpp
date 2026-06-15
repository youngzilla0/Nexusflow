#ifndef NEXUSFLOW_NODE_STATE_REGISTRY_HPP
#define NEXUSFLOW_NODE_STATE_REGISTRY_HPP

#include "statistics/Statistics.hpp"
#include "executor/JoinStateStore.hpp"
#include "base/Define.hpp"
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
 * @brief 维护 Executor 中各节点执行期状态的注册表。
 *
 * 该类只负责保存节点运行时状态、输入队列绑定关系与状态查询，
 * 不负责调度策略、线程池执行或输出分发。
 *
 * NodeStateRegistry 可以理解为 Executor 的“节点状态目录”：
 * - 创建并保存每个节点对应的 NodeState
 * - 绑定输入端口与消息队列
 * - 为 Statistics 暴露节点级统计状态
 * - 为调度器提供稳定的状态查询入口
 */
class NodeStateRegistry {
public:
    using PortStatsStatePtr = Statistics::PortStatsStatePtr;
    using NodeStatsState = Statistics::NodeStatsState;
    using NodeStatsStatePtr = Statistics::NodeStatsStatePtr;

    /**
     * @brief 输入端口与底层消息队列的绑定关系。
     *
     * Executor 在拉取输入时会遍历这里记录的绑定项，
     * 从而知道某个输入端口对应哪条消息队列以及哪份边级统计状态。
     */
    struct InputQueueBinding {
        std::string inputPortName; ///< 节点输入端口名称。
        ViewPtr<MessageQueue> queue; ///< 该输入端口对应的底层消息队列。
        PortStatsStatePtr stats; ///< 该输入边关联的边级统计状态；统计关闭时可为空。
    };

    /**
     * @brief 单个节点的执行期状态快照。
     *
     * 该结构聚合了 Executor 在调度一个节点时需要的全部热路径状态：
     * - module / runtimeConfig: 决定节点的执行语义
     * - inputQueues: 描述输入来源
     * - nextInputIndex: 维护 OnAnyInput 模式下的公平轮转位置
     * - joinState: 保存 OnAllInputs 模式下尚未拼齐的输入组
     * - taskScheduled / pendingRunSignals: 维护任务提交与补跑信号
     * - stats: 指向节点级统计状态
     */
    struct NodeState {
        std::string nodeName; ///< 节点名称，作为状态查找与统计归属的稳定标识。
        std::shared_ptr<Module> module; ///< 节点对应的业务模块实例。
        PipelineConfig runtimeConfig; ///< 节点运行时配置快照。
        std::vector<InputQueueBinding> inputQueues; ///< 节点全部输入端口的队列绑定关系。
        std::size_t nextInputIndex = 0; ///< OnAnyInput 模式下下一次优先扫描的输入索引，用于避免总是偏向第一个端口。
        JoinStateStore joinState; ///< OnAllInputs 模式下缓存尚未拼齐的 join group。
        std::atomic<bool> taskScheduled{false}; ///< 当前节点是否已经提交到线程池中执行或排队，避免重复提交。
        std::atomic<std::uint64_t> pendingRunSignals{0}; ///< 节点执行过程中累计收到的“仍需再跑一次”信号数。
        bool isSinkNode = false; ///< 当前节点在拓扑上是否为 sink 节点，用于决定是否记录端到端时延。
        NodeStatsStatePtr stats = std::make_shared<NodeStatsState>(); ///< 节点级统计状态对象。
    };

    using NodeStatePtr = std::shared_ptr<NodeState>;

    /**
     * @brief 注册一个节点的执行期状态。
     * @param nodeName 节点名称。
     * @param module 节点对应的模块实例。
     * @param runtimeConfig 节点运行时配置。
     * @param statistics 统计聚合器，用于登记该节点的统计状态。
     * @param isSinkNode 当前节点是否为 sink 节点。
     *
     * 该函数会：
     * - 创建 NodeState
     * - 放入内部注册表
     * - 将状态中的 stats 注册到 Statistics
     */
    void RegisterNode(const std::string& nodeName,
                      const std::shared_ptr<Module>& module,
                      const PipelineConfig& runtimeConfig,
                      Statistics& statistics,
                      bool isSinkNode);

    /**
     * @brief 为指定节点追加一个输入队列绑定。
     * @param nodeName 目标节点名称。
     * @param inputPortName 输入端口名称。
     * @param queue 输入端口对应的消息队列。
     * @param stats 该输入边对应的边级统计状态。
     */
    void AddInputQueue(const std::string& nodeName,
                       const std::string& inputPortName,
                       ViewPtr<MessageQueue> queue,
                       const PortStatsStatePtr& stats);

    /**
     * @brief 按节点名称查找执行期状态。
     * @param nodeName 目标节点名称。
     * @return 若存在则返回对应状态，否则返回空指针。
     */
    NodeStatePtr Find(const std::string& nodeName) const;

    /**
     * @brief 返回当前全部节点状态的稳定快照。
     * @return 所有已注册节点状态的共享指针列表。
     */
    std::vector<NodeStatePtr> SnapshotStates() const;

    /**
     * @brief 返回当前已注册节点数量。
     * @return 节点数量。
     */
    std::size_t Size() const;

private:
    mutable std::mutex m_mutex; ///< 保护整个状态注册表的互斥锁。
    std::unordered_map<std::string, NodeStatePtr> m_nodeStates; ///< 节点名称到执行期状态的映射表。
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_NODE_STATE_REGISTRY_HPP
