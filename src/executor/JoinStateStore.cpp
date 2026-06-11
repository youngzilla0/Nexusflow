#include "executor/JoinStateStore.hpp"

#include <limits>
#include <utility>

namespace nexusflow { namespace executor {

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

std::size_t JoinStateStore::EvictExpired(std::uint64_t currentTimeMs, std::uint64_t fusionTimeoutMs) {
    std::lock_guard<std::mutex> lock(m_mutex);

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

std::size_t JoinStateStore::EnforceLimit(std::size_t maxPendingJoinGroups) {
    if (maxPendingJoinGroups == 0) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

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

bool JoinStateStore::TakeCompleteInputs(const std::vector<std::string>& expectedInputPorts, std::vector<PortMessage>& inputs) {
    if (expectedInputPorts.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

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

std::uint64_t JoinStateStore::PendingGroupCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return static_cast<std::uint64_t>(m_pendingJoinGroups.size());
}

}} // namespace nexusflow::executor
