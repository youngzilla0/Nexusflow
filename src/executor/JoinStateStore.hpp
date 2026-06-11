#ifndef NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP
#define NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP

#include <nexusflow/Ports.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

// JoinStateStore 负责 OnAllInputs 的临时拼接状态。
// 它不关心调度，也不关心统计；只负责：
// - 按 join key 缓存不同输入端口的消息
// - 淘汰超时 group
// - 在超过上限时淘汰最老 group
// - 当一组输入齐全时取出一份可执行输入
class JoinStateStore {
public:
    void Insert(std::uint64_t joinKey, const std::string& inputPortName, Message message);
    std::size_t EvictExpired(std::uint64_t currentTimeMs, std::uint64_t fusionTimeoutMs);
    std::size_t EnforceLimit(std::size_t maxPendingJoinGroups);
    bool TakeCompleteInputs(const std::vector<std::string>& expectedInputPorts, std::vector<PortMessage>& inputs);
    std::uint64_t PendingGroupCount() const;

private:
    struct PendingJoinGroup {
        std::unordered_map<std::string, Message> messages;
        std::uint64_t oldestTimestampMs = 0;
    };

    mutable std::mutex m_mutex;
    std::unordered_map<std::uint64_t, PendingJoinGroup> m_pendingJoinGroups;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP
