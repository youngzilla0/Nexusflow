#ifndef NEXUSFLOW_EXECUTOR_HPP
#define NEXUSFLOW_EXECUTOR_HPP

#include "ThreadPool.hpp"
#include "base/Define.hpp"
#include "common/ViewPtr.hpp"

#include <nexusflow/Module.hpp>
#include <nexusflow/PipelineConfig.hpp>
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/RuntimeStats.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

class Executor {
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

    explicit Executor(const std::shared_ptr<PipelineContext>& pipelineContext);
    ~Executor();

    void RegisterActor(const std::string& actorName,
                       const std::shared_ptr<Module>& module,
                       const PipelineConfig& runtimeConfig);

    void AddInputQueue(const std::string& actorName, const std::string& inputPortName, ViewPtr<MessageQueue> queue,
                       const PortRuntimeStatsStatePtr& stats);
    void AddOutputQueue(const std::string& actorName, const std::string& outputPortName, const std::string& dstActorName,
                        const std::string& dstInputPortName, ViewPtr<MessageQueue> queue,
                        const PortRuntimeStatsStatePtr& stats);

    void SetThreadCount(std::size_t threadCount);

    void Emit(const std::string& actorName, const Message& message, bool blocking);
    void Route(const std::string& actorName, const std::string& outputPortName, const Message& message, bool blocking);
    std::vector<PortRuntimeStats> GetPortStats() const;
    std::vector<ActorRuntimeStats> GetActorStats() const;

    void Start();
    void Stop();

private:
    struct InputQueueBinding {
        std::string inputPortName;
        ViewPtr<MessageQueue> queue;
        PortRuntimeStatsStatePtr stats;
    };

    struct ActorState {
        struct PendingJoinGroup {
            std::unordered_map<std::string, Message> messages;
            std::uint64_t oldestTimestampMs = 0;
        };

        std::string actorName;
        std::shared_ptr<Module> module;
        PipelineConfig runtimeConfig;
        std::vector<InputQueueBinding> inputQueues;
        std::size_t nextInputIndex = 0;
        mutable std::mutex pendingJoinGroupsMutex;
        std::unordered_map<std::uint64_t, PendingJoinGroup> pendingJoinGroups;
        std::atomic<bool> taskScheduled{false};
        std::atomic<std::uint64_t> pendingRunSignals{0};
        ActorRuntimeStatsState runtimeStats;
    };

    struct OutputSubscriber {
        std::string dstActorName;
        std::string dstInputPortName;
        ViewPtr<MessageQueue> queue;
        PortRuntimeStatsStatePtr stats;
        std::shared_ptr<ActorState> dstActorState;
    };

private:
    std::uint64_t ResolveJoinKey(const ActorState& state, const Message& message) const;
    bool StatisticsEnabled() const;
    void PrimeActorsOnStart();
    void SubmitActorTask(const std::shared_ptr<ActorState>& state);
    void NotifyActorReady(const std::shared_ptr<ActorState>& state);
    bool HasPendingWork(const std::shared_ptr<ActorState>& state) const;
    void RunActorTask(const std::shared_ptr<ActorState>& state);
    bool RunSourceStep(const std::shared_ptr<ActorState>& state);
    bool RunOnAnyInputStep(const std::shared_ptr<ActorState>& state);
    bool RunOnAllInputsStep(const std::shared_ptr<ActorState>& state);
    bool TryPopAnyInput(const std::shared_ptr<ActorState>& state, PortMessage& portMessage);
    bool TryTakeCompleteJoinInputs(const std::shared_ptr<ActorState>& state, std::vector<PortMessage>& inputs);
    void CleanupExpiredJoinGroups(const std::shared_ptr<ActorState>& state, std::uint64_t currentTimeMs);
    void EnforcePendingJoinGroupLimit(const std::shared_ptr<ActorState>& state);
    void DispatchOutputs(const std::string& actorName, PortOutputs& outputs);
    void DispatchToSubscriber(const OutputSubscriber& subscriber, const Message& message, bool blocking);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<ActorState>> m_actorStates;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_broadcastSubscribers;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_outputSubscribers;
    std::vector<PortRuntimeStatsStatePtr> m_portStats;
    std::shared_ptr<PipelineContext> m_pipelineContext;
    std::unique_ptr<ThreadPool> m_threadPool;
    std::size_t m_threadCount = 0;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopFlag{false};
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_HPP
