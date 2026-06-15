#ifndef NEXUSFLOW_STATISTICS_RUNTIME_HPP
#define NEXUSFLOW_STATISTICS_RUNTIME_HPP

#include <nexusflow/StatisticsTypes.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace nexusflow { namespace executor {

/**
 * @brief 维护 Executor 运行时的统计状态与快照聚合逻辑。
 *
 * 该类处在“统计事件记录”和“对外快照输出”之间：
 * - 热路径调用 Record* 接口，只做轻量计数或样本附加
 * - Snapshot* 接口负责把内部状态转换成对外可读的统计结构
 *
 * 它本身不参与调度，不决定拓扑，只负责记录和汇总。
 */
class Statistics {
public:
    /**
     * @brief 单条边的运行时统计状态。
     *
     * 该状态由 PortRouter 在 push / dequeue 路径上更新，
     * 用于生成对外可见的 PortStats 快照。
     */
    struct PortStatsState {
        /**
         * @brief 构造一条边的统计状态。
         * @param srcModuleName 源节点名称。
         * @param srcPortName 源输出端口名称。
         * @param dstModuleName 目标节点名称。
         * @param dstPortName 目标输入端口名称。
         */
        PortStatsState(std::string srcModuleName, std::string srcPortName,
                       std::string dstModuleName, std::string dstPortName);

        /**
         * @brief 记录一次消息 push 尝试。
         * @param blocking 本次 push 是否为阻塞模式。
         */
        void RecordPushAttempt(bool blocking);

        /**
         * @brief 记录一次成功 push，并同步更新深度统计。
         * @param enqueuedCount 本次成功入队的消息数量。
         * @param droppedToMakeRoom 为腾出容量而被淘汰的旧消息数量。
         */
        void RecordPushAccepted(std::size_t enqueuedCount, std::size_t droppedToMakeRoom);

        /**
         * @brief 记录一次主动丢弃。
         * @param dropCount 本次丢弃的消息数量。
         */
        void RecordPushDropped(std::size_t dropCount);

        /**
         * @brief 记录一次 push 被拒绝。
         */
        void RecordPushRejected();

        /**
         * @brief 记录一次成功 dequeue。
         */
        void RecordDequeue();

        /**
         * @brief 生成当前边级统计快照。
         * @return 对外暴露的 PortStats 只读快照。
         */
        PortStats Snapshot() const;

        std::string srcModuleName; ///< 源节点名称。
        std::string srcPortName; ///< 源输出端口名称。
        std::string dstModuleName; ///< 目标节点名称。
        std::string dstPortName; ///< 目标输入端口名称。
        std::atomic<std::uint64_t> pushAttempts{0}; ///< 全部 push 尝试次数。
        std::atomic<std::uint64_t> blockingPushAttempts{0}; ///< 阻塞 push 尝试次数。
        std::atomic<std::uint64_t> nonBlockingPushAttempts{0}; ///< 非阻塞 push 尝试次数。
        std::atomic<std::uint64_t> enqueueCount{0}; ///< 成功入队次数。
        std::atomic<std::uint64_t> dropCount{0}; ///< 丢弃消息总数。
        std::atomic<std::uint64_t> rejectCount{0}; ///< 被拒绝消息总数。
        std::atomic<std::uint64_t> dequeueCount{0}; ///< 成功出队次数。
        std::atomic<std::uint64_t> depthAdditions{0}; ///< 累计入队增量，用于推导当前深度。
        std::atomic<std::uint64_t> depthSubtractions{0}; ///< 累计出队或淘汰减量，用于推导当前深度。
        std::atomic<std::uint64_t> peakDepth{0}; ///< 历史峰值深度。
    };

    using PortStatsStatePtr = std::shared_ptr<PortStatsState>;

    /**
     * @brief 单个节点的运行时统计状态。
     *
     * 该状态由 Executor 在节点执行路径上更新，
     * 保存节点处理次数、join 丢弃信息以及 sink 时延样本。
     */
    struct NodeStatsState {
        std::atomic<std::uint64_t> processCount{0}; ///< 节点实际执行 Process 的次数。
        std::atomic<std::uint64_t> inputMessageCount{0}; ///< 节点成功消费的输入消息总数。
        std::atomic<std::uint64_t> emittedBroadcastCount{0}; ///< 节点发出的广播输出次数。
        std::atomic<std::uint64_t> emittedRouteCount{0}; ///< 节点发出的定向路由输出次数。
        std::atomic<std::uint64_t> joinTimeoutDropCount{0}; ///< join 状态因超时被淘汰的输入组数量。
        std::atomic<std::uint64_t> joinOverflowDropCount{0}; ///< join 状态因容量上限被淘汰的输入组数量。
        std::atomic<std::uint64_t> sinkReceiveCount{0}; ///< sink 节点累计接收到的消息数量。
        mutable std::mutex latencyMutex; ///< 保护 sinkLatencySamplesMs 的互斥锁。
        std::vector<std::uint64_t> sinkLatencySamplesMs; ///< sink 节点端到端时延样本，单位毫秒。
    };

    using NodeStatsStatePtr = std::shared_ptr<NodeStatsState>;
    using PendingJoinGroupCountFn = std::function<std::uint64_t()>;

    /**
     * @brief 构造统计聚合器。
     * @param enabled 是否启用统计。
     */
    explicit Statistics(bool enabled);

    /**
     * @brief 返回统计功能是否启用。
     * @return 启用时返回 true。
     */
    bool Enabled() const { return m_enabled; }

    /**
     * @brief 注册一条边的统计状态。
     * @param stats 边级统计状态对象。
     */
    void RegisterPortStats(const PortStatsStatePtr& stats);

    /**
     * @brief 注册一个节点的统计状态。
     * @param nodeName 节点名称。
     * @param stats 节点统计状态对象。
     * @param pendingJoinGroupCountFn 返回当前 pending join group 数量的回调。
     * @param isSinkNode 当前节点是否为 sink 节点。
     */
    void RegisterNode(std::string nodeName,
                      const NodeStatsStatePtr& stats,
                      PendingJoinGroupCountFn pendingJoinGroupCountFn,
                      bool isSinkNode);

    /**
     * @brief 生成全部边级统计快照。
     * @return 当前全部边的统计快照列表。
     */
    std::vector<PortStats> SnapshotPorts() const;

    /**
     * @brief 生成全部节点级统计快照。
     * @return 当前全部节点的统计快照列表。
     */
    std::vector<NodeStats> SnapshotNodes() const;

    /**
     * @brief 为 sink 节点记录一条端到端时延样本。
     * @param nodeName sink 节点名称。
     * @param latencyMs 端到端时延，单位毫秒。
     */
    void RecordSinkLatency(const std::string& nodeName, std::uint64_t latencyMs);

    /**
     * @brief 聚合全部 sink 节点的时延样本。
     * @return 所有 sink 节点的时延样本列表。
     */
    std::vector<std::uint64_t> SnapshotSinkLatencies() const;

    /**
     * @brief 返回全部 sink 节点累计接收的消息数量。
     * @return sink 节点累计接收数。
     */
    std::uint64_t SnapshotSinkReceiveCount() const;

private:
    /**
     * @brief 节点统计注册信息。
     *
     * 该结构把节点名称、节点统计状态、join 状态查询回调和 sink 标记绑定在一起，
     * 便于在 Snapshot 阶段统一组装 NodeStats。
     */
    struct NodeRegistration {
        std::string nodeName; ///< 节点名称。
        NodeStatsStatePtr stats; ///< 节点统计状态对象。
        PendingJoinGroupCountFn pendingJoinGroupCountFn; ///< 查询当前 pending join group 数量的回调。
        bool isSinkNode = false; ///< 当前节点是否为 sink 节点。
    };

    bool m_enabled; ///< 统计总开关；关闭时所有注册与快照接口都快速返回。
    mutable std::mutex m_mutex; ///< 保护注册表与聚合快照读取的互斥锁。
    std::vector<PortStatsStatePtr> m_portStats; ///< 全部边级统计状态对象。
    std::vector<NodeRegistration> m_nodeRegistrations; ///< 全部节点级统计注册信息。
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_STATISTICS_RUNTIME_HPP
