#ifndef NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP
#define NEXUSFLOW_EXECUTOR_JOIN_STATE_STORE_HPP

#include <nexusflow/Ports.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

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
