#include "executor/JoinStateStore.hpp"

#include <limits>
#include <utility>

namespace nexusflow { namespace executor {

/**
 * @brief 向 join 缓存中插入一条输入消息。
 * @param joinKey 当前消息所属的 join key。
 * @param inputPortName 当前消息到达的输入端口名称。
 * @param message 待缓存消息。
 */
void JoinStateStore::Insert(std::uint64_t joinKey, const std::string& inputPortName, Message message) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto groupIt = m_pendingJoinGroups.find(joinKey);
    if (groupIt == m_pendingJoinGroups.end()) {
        groupIt = m_pendingJoinGroups.emplace(joinKey, PendingJoinGroup{}).first;
    }

    auto& group = groupIt->second;
    const auto messageTimestamp = message.GetMetaData().timestamp;
    if (group.oldestTimestampMs == 0 || messageTimestamp < group.oldestTimestampMs) {
        group.oldestTimestampMs = messageTimestamp;
    }
    group.messages[inputPortName] = std::move(message);
}

/**
 * @brief 淘汰超时的 pending join group。
 * @param currentTimeMs 当前时间戳，单位毫秒。
 * @param fusionTimeoutMs 允许保留的最大时长，单位毫秒。
 * @return 被淘汰的 group 数量。
 */
std::size_t JoinStateStore::EvictExpired(std::uint64_t currentTimeMs, std::uint64_t fusionTimeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);
    return EvictExpiredLocked(currentTimeMs, fusionTimeoutMs);
}

/**
 * @brief 在调用方已持锁的前提下淘汰超时的 pending join group。
 * @param currentTimeMs 当前时间戳，单位毫秒。
 * @param fusionTimeoutMs 允许保留的最大时长，单位毫秒。
 * @return 被淘汰的 group 数量。
 */
std::size_t JoinStateStore::EvictExpiredLocked(std::uint64_t currentTimeMs, std::uint64_t fusionTimeoutMs) {
    std::size_t evictedCount = 0;
    for (auto groupIt = m_pendingJoinGroups.begin(); groupIt != m_pendingJoinGroups.end();) {
        const auto oldestTimestampMs = groupIt->second.oldestTimestampMs;
        if (oldestTimestampMs + fusionTimeoutMs < currentTimeMs) {
            groupIt = m_pendingJoinGroups.erase(groupIt);
            ++evictedCount;
        } else {
            ++groupIt;
        }
    }
    return evictedCount;
}

/**
 * @brief 将 pending join group 数量限制在给定上限内。
 * @param maxPendingJoinGroups 允许保留的最大 group 数量。
 * @return 因超出上限而被淘汰的 group 数量。
 */
std::size_t JoinStateStore::EnforceLimit(std::size_t maxPendingJoinGroups) {
    if (maxPendingJoinGroups == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    return EnforceLimitLocked(maxPendingJoinGroups);
}

/**
 * @brief 在调用方已持锁的前提下限制 pending join group 数量。
 * @param maxPendingJoinGroups 允许保留的最大 group 数量。
 * @return 因超出上限而被淘汰的 group 数量。
 */
std::size_t JoinStateStore::EnforceLimitLocked(std::size_t maxPendingJoinGroups) {
    std::size_t evictedCount = 0;
    while (m_pendingJoinGroups.size() > maxPendingJoinGroups) {
        auto oldestIt = m_pendingJoinGroups.end();
        auto oldestTimestamp = std::numeric_limits<std::uint64_t>::max();
        for (auto it = m_pendingJoinGroups.begin(); it != m_pendingJoinGroups.end(); ++it) {
            if (it->second.oldestTimestampMs < oldestTimestamp) {
                oldestTimestamp = it->second.oldestTimestampMs;
                oldestIt = it;
            }
        }

        if (oldestIt == m_pendingJoinGroups.end()) {
            break;
        }
        m_pendingJoinGroups.erase(oldestIt);
        ++evictedCount;
    }
    return evictedCount;
}

/**
 * @brief 提取一组完整的 join 输入。
 * @param expectedInputPorts 当前模块要求到齐的全部输入端口。
 * @param inputs 输出参数，用于接收可执行的一组输入。
 * @return 成功提取完整输入组时返回 true。
 */
bool JoinStateStore::TakeCompleteInputs(const std::vector<std::string>& expectedInputPorts, std::vector<PortMessage>& inputs) {
    if (expectedInputPorts.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    return TakeCompleteInputsLocked(expectedInputPorts, inputs);
}

/**
 * @brief 在调用方已持锁的前提下提取一组完整的 join 输入。
 * @param expectedInputPorts 当前模块要求到齐的全部输入端口。
 * @param inputs 输出参数，用于接收可执行的一组输入。
 * @return 成功提取完整输入组时返回 true。
 */
bool JoinStateStore::TakeCompleteInputsLocked(const std::vector<std::string>& expectedInputPorts,
                                              std::vector<PortMessage>& inputs) {
    for (auto groupIt = m_pendingJoinGroups.begin(); groupIt != m_pendingJoinGroups.end(); ++groupIt) {
        auto& group = groupIt->second;
        if (group.messages.size() != expectedInputPorts.size()) {
            continue;
        }

        inputs.clear();
        inputs.reserve(expectedInputPorts.size());
        for (const auto& inputPortName : expectedInputPorts) {
            auto messageIt = group.messages.find(inputPortName);
            if (messageIt != group.messages.end()) {
                inputs.push_back(PortMessage{inputPortName, std::move(messageIt->second)});
            }
        }
        m_pendingJoinGroups.erase(groupIt);
        return true;
    }

    return false;
}

/**
 * @brief 在一次锁保护下完成 join 清理、限流与完整组提取。
 * @param expectedInputPorts 当前模块要求到齐的全部输入端口。
 * @param currentTimeMs 当前时间戳，单位毫秒。
 * @param fusionTimeoutMs join group 允许保留的最大时长，单位毫秒。
 * @param maxPendingJoinGroups 允许保留的最大 group 数量。
 * @param inputs 输出参数，用于接收可执行的一组输入。
 * @return 本次 sweep 的综合结果。
 */
JoinStateStore::SweepResult JoinStateStore::SweepAndTakeCompleteInputs(const std::vector<std::string>& expectedInputPorts,
                                                                       std::uint64_t currentTimeMs,
                                                                       std::uint64_t fusionTimeoutMs,
                                                                       std::size_t maxPendingJoinGroups,
                                                                       std::vector<PortMessage>& inputs) {
    SweepResult result;
    if (expectedInputPorts.empty()) {
        return result;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    result.expiredGroupCount = EvictExpiredLocked(currentTimeMs, fusionTimeoutMs);
    result.overflowGroupCount = EnforceLimitLocked(maxPendingJoinGroups);
    result.tookCompleteGroup = TakeCompleteInputsLocked(expectedInputPorts, inputs);
    return result;
}

/**
 * @brief 返回当前 pending join group 数量。
 * @return 当前 pending group 数量。
 */
std::uint64_t JoinStateStore::PendingGroupCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<std::uint64_t>(m_pendingJoinGroups.size());
}

}} // namespace nexusflow::executor
