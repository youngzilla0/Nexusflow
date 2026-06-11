#ifndef NEXUSFLOW_EXECUTOR_HPP
#define NEXUSFLOW_EXECUTOR_HPP

#include "JoinStateStore.hpp"
#include "RuntimeStatsCollector.hpp"
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
    using PortRuntimeStatsState = RuntimeStatsCollector::PortRuntimeStatsState;
    using PortRuntimeStatsStatePtr = RuntimeStatsCollector::PortRuntimeStatsStatePtr;
    using ActorRuntimeStatsState = RuntimeStatsCollector::ActorRuntimeStatsState;
    using ActorRuntimeStatsStatePtr = RuntimeStatsCollector::ActorRuntimeStatsStatePtr;

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
        std::string actorName;
        std::shared_ptr<Module> module;
        PipelineConfig runtimeConfig;
        std::vector<InputQueueBinding> inputQueues;
        std::size_t nextInputIndex = 0;
        JoinStateStore joinState;
        std::atomic<bool> taskScheduled{false};
        std::atomic<std::uint64_t> pendingRunSignals{0};
        ActorRuntimeStatsStatePtr runtimeStats = std::make_shared<ActorRuntimeStatsState>();
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
    void DispatchOutputs(const std::string& actorName, PortOutputs& outputs);
    void DispatchToSubscriber(const OutputSubscriber& subscriber, const Message& message, bool blocking);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<ActorState>> m_actorStates;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_broadcastSubscribers;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_outputSubscribers;
    RuntimeStatsCollector m_statsCollector;
    std::shared_ptr<PipelineContext> m_pipelineContext;
    std::unique_ptr<ThreadPool> m_threadPool;
    std::size_t m_threadCount = 0;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopFlag{false};
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_HPP
