#include "statistics/Statistics.hpp"

#include <algorithm>
#include <utility>

namespace nexusflow { namespace executor {

/**
 * @brief 构造一条边的统计状态。
 * @param srcModuleNameValue 源节点名称。
 * @param srcPortNameValue 源输出端口名称。
 * @param dstModuleNameValue 目标节点名称。
 * @param dstPortNameValue 目标输入端口名称。
 */
Statistics::PortStatsState::PortStatsState(std::string srcModuleNameValue, std::string srcPortNameValue,
                                           std::string dstModuleNameValue, std::string dstPortNameValue)
    : srcModuleName(std::move(srcModuleNameValue)),
      srcPortName(std::move(srcPortNameValue)),
      dstModuleName(std::move(dstModuleNameValue)),
      dstPortName(std::move(dstPortNameValue)) {}

/**
 * @brief 记录一次 push 尝试。
 * @param blocking 本次 push 是否为阻塞模式。
 */
void Statistics::PortStatsState::RecordPushAttempt(bool blocking) {
    pushAttempts.fetch_add(1, std::memory_order_relaxed);
    if (blocking) {
        blockingPushAttempts.fetch_add(1, std::memory_order_relaxed);
    } else {
        nonBlockingPushAttempts.fetch_add(1, std::memory_order_relaxed);
    }
}

/**
 * @brief 记录一次成功 push，并更新当前深度与峰值深度。
 * @param enqueuedCount 本次成功入队的消息数量。
 * @param droppedToMakeRoom 为腾出容量而被淘汰的旧消息数量。
 */
void Statistics::PortStatsState::RecordPushAccepted(std::size_t enqueuedCount, std::size_t droppedToMakeRoom) {
    enqueueCount.fetch_add(1, std::memory_order_relaxed);
    if (droppedToMakeRoom > 0) {
        dropCount.fetch_add(static_cast<std::uint64_t>(droppedToMakeRoom), std::memory_order_relaxed);
        depthSubtractions.fetch_add(static_cast<std::uint64_t>(droppedToMakeRoom), std::memory_order_relaxed);
    }

    const auto additionsAfter =
        depthAdditions.fetch_add(static_cast<std::uint64_t>(enqueuedCount), std::memory_order_relaxed) +
        static_cast<std::uint64_t>(enqueuedCount);
    const auto subtractionsNow = depthSubtractions.load(std::memory_order_relaxed);
    const auto depthAfter = additionsAfter > subtractionsNow ? additionsAfter - subtractionsNow : 0;

    auto previousPeak = peakDepth.load(std::memory_order_relaxed);
    while (depthAfter > previousPeak &&
           !peakDepth.compare_exchange_weak(previousPeak, depthAfter, std::memory_order_relaxed)) {
    }
}

/**
 * @brief 记录一次主动丢弃。
 * @param dropCountValue 本次丢弃的消息数量。
 */
void Statistics::PortStatsState::RecordPushDropped(std::size_t dropCountValue) {
    dropCount.fetch_add(static_cast<std::uint64_t>(dropCountValue), std::memory_order_relaxed);
}

/**
 * @brief 记录一次 push 被拒绝。
 */
void Statistics::PortStatsState::RecordPushRejected() {
    rejectCount.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief 记录一次成功 dequeue。
 */
void Statistics::PortStatsState::RecordDequeue() {
    dequeueCount.fetch_add(1, std::memory_order_relaxed);
    depthSubtractions.fetch_add(1, std::memory_order_relaxed);
}

/**
 * @brief 生成当前边级统计快照。
 * @return 当前边的只读统计快照。
 */
PortStats Statistics::PortStatsState::Snapshot() const {
    PortStats snapshot;
    snapshot.srcModuleName = srcModuleName;
    snapshot.srcPortName = srcPortName;
    snapshot.dstModuleName = dstModuleName;
    snapshot.dstPortName = dstPortName;
    snapshot.pushAttempts = pushAttempts.load(std::memory_order_relaxed);
    snapshot.blockingPushAttempts = blockingPushAttempts.load(std::memory_order_relaxed);
    snapshot.nonBlockingPushAttempts = nonBlockingPushAttempts.load(std::memory_order_relaxed);
    snapshot.enqueueCount = enqueueCount.load(std::memory_order_relaxed);
    snapshot.dropCount = dropCount.load(std::memory_order_relaxed);
    snapshot.rejectCount = rejectCount.load(std::memory_order_relaxed);
    snapshot.dequeueCount = dequeueCount.load(std::memory_order_relaxed);
    const auto additions = depthAdditions.load(std::memory_order_relaxed);
    const auto subtractions = depthSubtractions.load(std::memory_order_relaxed);
    snapshot.currentDepth = additions > subtractions ? additions - subtractions : 0;
    snapshot.peakDepth = peakDepth.load(std::memory_order_relaxed);
    return snapshot;
}

/**
 * @brief 构造统计聚合器。
 * @param enabled 是否启用统计。
 */
Statistics::Statistics(bool enabled) : m_enabled(enabled) {}

/**
 * @brief 注册一条边的统计状态。
 * @param stats 边级统计状态对象。
 */
void Statistics::RegisterPortStats(const PortStatsStatePtr& stats) {
    if (!m_enabled || stats == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto duplicateIt = std::find_if(m_portStats.begin(), m_portStats.end(),
                                    [&stats](const PortStatsStatePtr& existing) {
                                        return existing.get() == stats.get();
                                    });
    if (duplicateIt == m_portStats.end()) {
        m_portStats.push_back(stats);
    }
}

/**
 * @brief 注册一个节点的统计状态。
 * @param nodeName 节点名称。
 * @param stats 节点统计状态对象。
 * @param pendingJoinGroupCountFn 查询 pending join group 数量的回调。
 * @param isSinkNode 当前节点是否为 sink 节点。
 */
void Statistics::RegisterNode(std::string nodeName, const NodeStatsStatePtr& stats,
                              PendingJoinGroupCountFn pendingJoinGroupCountFn,
                              bool isSinkNode) {
    if (!m_enabled || stats == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_nodeRegistrations.push_back(NodeRegistration{std::move(nodeName), stats, std::move(pendingJoinGroupCountFn), isSinkNode});
}

/**
 * @brief 生成全部边级统计快照。
 * @return 当前全部边级统计快照列表。
 */
std::vector<PortStats> Statistics::SnapshotPorts() const {
    if (!m_enabled) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<PortStats> snapshots;
    snapshots.reserve(m_portStats.size());
    for (const auto& stats : m_portStats) {
        if (stats != nullptr) {
            snapshots.push_back(stats->Snapshot());
        }
    }
    return snapshots;
}

/**
 * @brief 生成全部节点级统计快照。
 * @return 当前全部节点级统计快照列表。
 */
std::vector<NodeStats> Statistics::SnapshotNodes() const {
    if (!m_enabled) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<NodeStats> snapshots;
    snapshots.reserve(m_nodeRegistrations.size());
    for (const auto& registration : m_nodeRegistrations) {
        if (registration.stats == nullptr) {
            continue;
        }

        NodeStats snapshot;
        snapshot.nodeName = registration.nodeName;
        snapshot.processCount = registration.stats->processCount.load(std::memory_order_relaxed);
        snapshot.inputMessageCount = registration.stats->inputMessageCount.load(std::memory_order_relaxed);
        snapshot.emittedBroadcastCount = registration.stats->emittedBroadcastCount.load(std::memory_order_relaxed);
        snapshot.emittedRouteCount = registration.stats->emittedRouteCount.load(std::memory_order_relaxed);
        snapshot.joinTimeoutDropCount = registration.stats->joinTimeoutDropCount.load(std::memory_order_relaxed);
        snapshot.joinOverflowDropCount = registration.stats->joinOverflowDropCount.load(std::memory_order_relaxed);
        if (registration.pendingJoinGroupCountFn) {
            snapshot.pendingJoinGroupCount = registration.pendingJoinGroupCountFn();
        }
        snapshots.push_back(std::move(snapshot));
    }
    return snapshots;
}

/**
 * @brief 为 sink 节点记录一条端到端时延样本。
 * @param nodeName sink 节点名称。
 * @param latencyMs 端到端时延，单位毫秒。
 */
void Statistics::RecordSinkLatency(const std::string& nodeName, std::uint64_t latencyMs) {
    if (!m_enabled) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& registration : m_nodeRegistrations) {
        if (registration.nodeName != nodeName || registration.stats == nullptr) {
            continue;
        }

        registration.stats->sinkReceiveCount.fetch_add(1, std::memory_order_relaxed);
        std::lock_guard<std::mutex> latencyLock(registration.stats->latencyMutex);
        registration.stats->sinkLatencySamplesMs.push_back(latencyMs);
        return;
    }
}

/**
 * @brief 聚合全部 sink 节点的时延样本。
 * @return sink 节点时延样本列表。
 */
std::vector<std::uint64_t> Statistics::SnapshotSinkLatencies() const {
    if (!m_enabled) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::uint64_t> samples;
    for (const auto& registration : m_nodeRegistrations) {
        if (!registration.isSinkNode || registration.stats == nullptr) {
            continue;
        }

        std::lock_guard<std::mutex> latencyLock(registration.stats->latencyMutex);
        samples.insert(samples.end(),
                       registration.stats->sinkLatencySamplesMs.begin(),
                       registration.stats->sinkLatencySamplesMs.end());
    }
    return samples;
}

/**
 * @brief 返回全部 sink 节点累计接收的消息数。
 * @return sink 节点累计接收数。
 */
std::uint64_t Statistics::SnapshotSinkReceiveCount() const {
    if (!m_enabled) {
        return 0;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    std::uint64_t count = 0;
    for (const auto& registration : m_nodeRegistrations) {
        if (!registration.isSinkNode || registration.stats == nullptr) {
            continue;
        }
        count += registration.stats->sinkReceiveCount.load(std::memory_order_relaxed);
    }
    return count;
}

}} // namespace nexusflow::executor
