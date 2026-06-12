#ifndef NEXUSFLOW_EXECUTOR_RUNTIME_STATS_COLLECTOR_HPP
#define NEXUSFLOW_EXECUTOR_RUNTIME_STATS_COLLECTOR_HPP

#include <nexusflow/RuntimeStats.hpp>

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
 * @brief 聚合 Executor 运行时统计信息。
 *
 * Executor 与 queue 在热路径上仅负责记录事件；
 * 对外暴露的快照由该类统一聚合和组装。
 */
class RuntimeStatsCollector {
public:
    /**
     * @brief 单条边的运行时统计状态。
     */
    struct PortRuntimeStatsState {
        /**
         * @brief 构造一份边级统计状态。
         * @param srcModuleName 源模块名称。
         * @param srcPortName 源输出端口名称。
         * @param dstModuleName 目标模块名称。
         * @param dstPortName 目标输入端口名称。
         */
        PortRuntimeStatsState(std::string srcModuleName, std::string srcPortName,
                              std::string dstModuleName, std::string dstPortName);

        /** @brief 记录一次 push 尝试。 */
        void RecordPushAttempt(bool blocking);
        /** @brief 记录一次成功 push，并更新深度统计。 */
        void RecordPushAccepted(std::size_t enqueuedCount, std::size_t droppedToMakeRoom);
        /** @brief 记录一次因容量策略导致的丢弃。 */
        void RecordPushDropped(std::size_t dropCount);
        /** @brief 记录一次因关闭等原因导致的拒绝。 */
        void RecordPushRejected();
        /** @brief 记录一次成功 dequeue。 */
        void RecordDequeue();

        /**
         * @brief 生成当前边级统计快照。
         * @return 当前边的只读统计快照。
         */
        PortRuntimeStats Snapshot() const;

        std::string srcModuleName;
        std::string srcPortName;
        std::string dstModuleName;
        std::string dstPortName;
        std::atomic<std::uint64_t> pushAttempts{0};
        std::atomic<std::uint64_t> blockingPushAttempts{0};
        std::atomic<std::uint64_t> nonBlockingPushAttempts{0};
        std::atomic<std::uint64_t> enqueueCount{0};
        std::atomic<std::uint64_t> dropCount{0};
        std::atomic<std::uint64_t> rejectCount{0};
        std::atomic<std::uint64_t> dequeueCount{0};
        std::atomic<std::uint64_t> depthAdditions{0};
        std::atomic<std::uint64_t> depthSubtractions{0};
        std::atomic<std::uint64_t> peakDepth{0};
    };

    using PortRuntimeStatsStatePtr = std::shared_ptr<PortRuntimeStatsState>;

    /**
     * @brief 单个 actor 的运行时统计状态。
     */
    struct ActorRuntimeStatsState {
        std::atomic<std::uint64_t> processCount{0};
        std::atomic<std::uint64_t> inputMessageCount{0};
        std::atomic<std::uint64_t> emittedBroadcastCount{0};
        std::atomic<std::uint64_t> emittedRouteCount{0};
        std::atomic<std::uint64_t> joinTimeoutDropCount{0};
        std::atomic<std::uint64_t> joinOverflowDropCount{0};
    };

    using ActorRuntimeStatsStatePtr = std::shared_ptr<ActorRuntimeStatsState>;
    using PendingJoinGroupCountFn = std::function<std::uint64_t()>;

    /**
     * @brief 构造统计聚合器。
     * @param enabled 是否启用统计聚合。
     */
    explicit RuntimeStatsCollector(bool enabled);

    /**
     * @brief 返回统计功能是否启用。
     * @return 启用时返回 true。
     */
    bool Enabled() const { return m_enabled; }

    /**
     * @brief 注册一条边的统计状态。
     * @param stats 边级统计状态对象。
     */
    void RegisterPortStats(const PortRuntimeStatsStatePtr& stats);

    /**
     * @brief 注册一个 actor 的统计状态。
     * @param actorName actor 名称。
     * @param stats actor 统计状态对象。
     * @param pendingJoinGroupCountFn 用于查询当前 pending join group 数量的回调。
     */
    void RegisterActor(std::string actorName, const ActorRuntimeStatsStatePtr& stats, PendingJoinGroupCountFn pendingJoinGroupCountFn);

    /**
     * @brief 生成全部边级统计快照。
     * @return 当前所有边的统计快照列表。
     */
    std::vector<PortRuntimeStats> SnapshotPorts() const;

    /**
     * @brief 生成全部 actor 级统计快照。
     * @return 当前所有 actor 的统计快照列表。
     */
    std::vector<ActorRuntimeStats> SnapshotActors() const;

private:
    /**
     * @brief actor 统计注册信息。
     */
    struct ActorRegistration {
        std::string actorName;
        ActorRuntimeStatsStatePtr stats;
        PendingJoinGroupCountFn pendingJoinGroupCountFn;
    };

    bool m_enabled;
    mutable std::mutex m_mutex;
    std::vector<PortRuntimeStatsStatePtr> m_portStats;
    std::vector<ActorRegistration> m_actorRegistrations;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_RUNTIME_STATS_COLLECTOR_HPP
