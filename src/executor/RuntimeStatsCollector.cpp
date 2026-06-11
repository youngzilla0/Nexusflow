#include "executor/RuntimeStatsCollector.hpp"

#include <algorithm>
#include <utility>

namespace nexusflow { namespace executor {

RuntimeStatsCollector::PortRuntimeStatsState::PortRuntimeStatsState(std::string srcModuleNameValue, std::string srcPortNameValue,
                                                                    std::string dstModuleNameValue, std::string dstPortNameValue)
    : srcModuleName(std::move(srcModuleNameValue)),
      srcPortName(std::move(srcPortNameValue)),
      dstModuleName(std::move(dstModuleNameValue)),
      dstPortName(std::move(dstPortNameValue)) {}

void RuntimeStatsCollector::PortRuntimeStatsState::RecordPushAttempt(bool blocking) {
    pushAttempts.fetch_add(1, std::memory_order_relaxed);
    if (blocking) {
        blockingPushAttempts.fetch_add(1, std::memory_order_relaxed);
    } else {
        nonBlockingPushAttempts.fetch_add(1, std::memory_order_relaxed);
    }
}

void RuntimeStatsCollector::PortRuntimeStatsState::RecordPushAccepted(std::size_t enqueuedCount, std::size_t droppedToMakeRoom) {
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

void RuntimeStatsCollector::PortRuntimeStatsState::RecordPushDropped(std::size_t dropCountValue) {
    dropCount.fetch_add(static_cast<std::uint64_t>(dropCountValue), std::memory_order_relaxed);
}

void RuntimeStatsCollector::PortRuntimeStatsState::RecordPushRejected() {
    rejectCount.fetch_add(1, std::memory_order_relaxed);
}

void RuntimeStatsCollector::PortRuntimeStatsState::RecordDequeue() {
    dequeueCount.fetch_add(1, std::memory_order_relaxed);
    depthSubtractions.fetch_add(1, std::memory_order_relaxed);
}

PortRuntimeStats RuntimeStatsCollector::PortRuntimeStatsState::Snapshot() const {
    PortRuntimeStats snapshot;
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

RuntimeStatsCollector::RuntimeStatsCollector(bool enabled) : m_enabled(enabled) {}

void RuntimeStatsCollector::RegisterPortStats(const PortRuntimeStatsStatePtr& stats) {
    if (!m_enabled || stats == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    auto duplicateIt = std::find_if(m_portStats.begin(), m_portStats.end(),
                                    [&stats](const PortRuntimeStatsStatePtr& existing) {
                                        return existing.get() == stats.get();
                                    });
    if (duplicateIt == m_portStats.end()) {
        m_portStats.push_back(stats);
    }
}

void RuntimeStatsCollector::RegisterActor(std::string actorName, const ActorRuntimeStatsStatePtr& stats,
                                          PendingJoinGroupCountFn pendingJoinGroupCountFn) {
    if (!m_enabled || stats == nullptr) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    m_actorRegistrations.push_back(ActorRegistration{std::move(actorName), stats, std::move(pendingJoinGroupCountFn)});
}

std::vector<PortRuntimeStats> RuntimeStatsCollector::SnapshotPorts() const {
    if (!m_enabled) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<PortRuntimeStats> snapshots;
    snapshots.reserve(m_portStats.size());
    for (const auto& stats : m_portStats) {
        if (stats != nullptr) {
            snapshots.push_back(stats->Snapshot());
        }
    }
    return snapshots;
}

std::vector<ActorRuntimeStats> RuntimeStatsCollector::SnapshotActors() const {
    if (!m_enabled) {
        return {};
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<ActorRuntimeStats> snapshots;
    snapshots.reserve(m_actorRegistrations.size());
    for (const auto& registration : m_actorRegistrations) {
        if (registration.stats == nullptr) {
            continue;
        }

        ActorRuntimeStats snapshot;
        snapshot.actorName = registration.actorName;
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

}} // namespace nexusflow::executor
