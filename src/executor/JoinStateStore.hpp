#ifndef NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP
#define NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP

#include <nexusflow/Ports.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

/**
 * @brief 管理 OnAllInputs 模式下的待拼接输入状态。
 *
 * 该类仅负责 join group 的缓存、淘汰与提取，不参与调度或统计逻辑。
 */
class JoinStateStore {
public:
    /**
     * @brief 单次 join 清理与提取操作的结果。
     *
     * 该结构把热路径里关心的三个结果聚合到一起：
     * - 是否淘汰了超时 group
     * - 是否因容量上限淘汰了旧 group
     * - 是否成功取出了一组完整输入
     */
    struct SweepResult {
        std::size_t expiredGroupCount = 0;
        std::size_t overflowGroupCount = 0;
        bool tookCompleteGroup = false;
    };

    /**
     * @brief 向指定 join group 插入一条输入消息。
     * @param joinKey 当前消息所属的 join key。
     * @param inputPortName 当前消息到达的输入端口名。
     * @param message 要缓存的消息对象。
     */
    void Insert(std::uint64_t joinKey, const std::string& inputPortName, Message message);

    /**
     * @brief 淘汰已超时的 pending join group。
     * @param currentTimeMs 当前时间戳，单位毫秒。
     * @param fusionTimeoutMs join group 允许保留的最大时长，单位毫秒。
     * @return 被淘汰的 group 数量。
     */
    std::size_t EvictExpired(std::uint64_t currentTimeMs, std::uint64_t fusionTimeoutMs);

    /**
     * @brief 将 pending join group 数量限制在给定上限内。
     * @param maxPendingJoinGroups 允许保留的最大 group 数量。
     * @return 因超过上限而被淘汰的 group 数量。
     */
    std::size_t EnforceLimit(std::size_t maxPendingJoinGroups);

    /**
     * @brief 尝试提取一组完整的 join 输入。
     * @param expectedInputPorts 当前模块要求到齐的全部输入端口。
     * @param inputs 输出参数，用于接收可执行的一组输入。
     * @return 当存在完整 join group 时返回 true，否则返回 false。
     */
    bool TakeCompleteInputs(const std::vector<std::string>& expectedInputPorts, std::vector<PortMessage>& inputs);

    /**
     * @brief 在一次锁保护下完成清理、限流和完整组提取。
     * @param expectedInputPorts 当前模块要求到齐的全部输入端口。
     * @param currentTimeMs 当前时间戳，单位毫秒。
     * @param fusionTimeoutMs join group 允许保留的最大时长，单位毫秒。
     * @param maxPendingJoinGroups 允许保留的最大 group 数量。
     * @param inputs 输出参数，用于接收可执行的一组输入。
     * @return 本次 sweep 的综合结果。
     */
    SweepResult SweepAndTakeCompleteInputs(const std::vector<std::string>& expectedInputPorts,
                                          std::uint64_t currentTimeMs,
                                          std::uint64_t fusionTimeoutMs,
                                          std::size_t maxPendingJoinGroups,
                                          std::vector<PortMessage>& inputs);

    /**
     * @brief 返回当前仍处于 pending 状态的 join group 数量。
     * @return pending join group 数量。
     */
    std::uint64_t PendingGroupCount() const;

private:
    std::size_t EvictExpiredLocked(std::uint64_t currentTimeMs, std::uint64_t fusionTimeoutMs);
    std::size_t EnforceLimitLocked(std::size_t maxPendingJoinGroups);
    bool TakeCompleteInputsLocked(const std::vector<std::string>& expectedInputPorts, std::vector<PortMessage>& inputs);

    /**
     * @brief 单个 pending join group 的缓存状态。
     */
    struct PendingJoinGroup {
        std::unordered_map<std::string, Message> messages;
        std::uint64_t oldestTimestampMs = 0;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, PendingJoinGroup> m_pendingJoinGroups;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP
