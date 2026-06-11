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

class RuntimeStatsCollector {
public:
    struct PortRuntimeStatsState {
        PortRuntimeStatsState(std::string srcModuleName, std::string srcPortName,
                              std::string dstModuleName, std::string dstPortName);

        void RecordPushAttempt(bool blocking);
        void RecordPushAccepted(std::size_t enqueuedCount, std::size_t droppedToMakeRoom);
        void RecordPushDropped(std::size_t dropCount);
        void RecordPushRejected();
        void RecordDequeue();

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

    explicit RuntimeStatsCollector(bool enabled);

    bool Enabled() const { return m_enabled; }

    void RegisterPortStats(const PortRuntimeStatsStatePtr& stats);
    void RegisterActor(std::string actorName, const ActorRuntimeStatsStatePtr& stats, PendingJoinGroupCountFn pendingJoinGroupCountFn);

    std::vector<PortRuntimeStats> SnapshotPorts() const;
    std::vector<ActorRuntimeStats> SnapshotActors() const;

private:
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
