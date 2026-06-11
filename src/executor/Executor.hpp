#ifndef NEXUSFLOW_EXECUTOR_HPP
#define NEXUSFLOW_EXECUTOR_HPP

#include "JoinStateStore.hpp"
#include "RuntimeStatsCollector.hpp"
#include "SchedulingPolicy.hpp"
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

// Executor 是当前运行时的核心协调器：
// - 管 actor/module 注册关系
// - 管输入输出队列绑定
// - 把 actor 提交给线程池执行
// - 在执行过程中调用 join/store 与 stats collector
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
    // 一个输入端口和底层队列的绑定关系。
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
        std::size_t nextInputIndex = 0; // OnAnyInput 下用于轮转扫描输入队列，避免总是偏向第一个端口。
        JoinStateStore joinState;       // 只在 OnAllInputs 下使用，缓存待拼齐的 join group。
        std::atomic<bool> taskScheduled{false}; // 当前 actor 是否已经在池中排队或执行，避免重复提交。
        std::atomic<std::uint64_t> pendingRunSignals{0}; // 记录执行期间新增的“需要再跑一次”信号。
        ActorRuntimeStatsStatePtr runtimeStats = std::make_shared<ActorRuntimeStatsState>();
    };

    // 一个输出订阅者对应一条边：srcActor:out -> dstActor:in。
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
    std::unique_ptr<SchedulingPolicy> m_schedulingPolicy;
    std::size_t m_threadCount = 0;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopFlag{false};
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_HPP
