#include "executor/Executor.hpp"

#include "utils/logging.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace nexusflow { namespace executor {

namespace {

std::string MakeOutputKey(const std::string& actorName, const std::string& outputPortName) {
    return actorName + "\n" + outputPortName;
}

uint64_t GetCurrentSystemTimeMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

std::size_t ResolveThreadCount(std::size_t configuredThreadCount, std::size_t actorCount) {
    (void)actorCount;
    if (configuredThreadCount > 0) {
        return configuredThreadCount;
    }

    auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    if (hardwareThreads == 0) hardwareThreads = 1;
    return std::max<std::size_t>(hardwareThreads, 1);
}

} // namespace

Executor::Executor(const std::shared_ptr<PipelineContext>& pipelineContext)
    : m_statsCollector(pipelineContext == nullptr || pipelineContext->IsStatisticsEnabled()), m_pipelineContext(pipelineContext) {}

Executor::~Executor() { Stop(); }

void Executor::RegisterActor(const std::string& actorName, const std::shared_ptr<Module>& module,
                             const PipelineConfig& runtimeConfig) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_actorStates.find(actorName) != m_actorStates.end()) {
        LOG_ERROR("Actor with name '{}' already exists", actorName);
        throw std::invalid_argument("Actor with name " + actorName + " already exists");
    }

    auto state = std::make_shared<ActorState>();
    state->actorName = actorName;
    state->module = module;
    state->runtimeConfig = runtimeConfig;
    auto actorState = state;
    m_actorStates.emplace(actorName, std::move(state));
    m_statsCollector.RegisterActor(
        actorName, actorState->runtimeStats,
        [state = std::move(actorState)]() -> std::uint64_t {
            return state->joinState.PendingGroupCount();
        });
}

void Executor::AddInputQueue(const std::string& actorName, const std::string& inputPortName, ViewPtr<MessageQueue> queue,
                             const PortRuntimeStatsStatePtr& stats) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_actorStates.find(actorName);
    if (it == m_actorStates.end()) {
        throw std::invalid_argument("Unknown actor " + actorName);
    }

    it->second->inputQueues.push_back(InputQueueBinding{inputPortName, queue, stats});
}

void Executor::AddOutputQueue(const std::string& actorName, const std::string& outputPortName, const std::string& dstActorName,
                              const std::string& dstInputPortName, ViewPtr<MessageQueue> queue,
                              const PortRuntimeStatsStatePtr& stats) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto portStats = StatisticsEnabled() ? stats : nullptr;
    if (StatisticsEnabled() && portStats == nullptr) {
        portStats = std::make_shared<PortRuntimeStatsState>(actorName, outputPortName, dstActorName, dstInputPortName);
    }

    auto dstIt = m_actorStates.find(dstActorName);
    if (dstIt == m_actorStates.end()) {
        throw std::invalid_argument("Unknown actor " + dstActorName);
    }

    OutputSubscriber subscriber{dstActorName, dstInputPortName, queue, portStats, dstIt->second};
    m_broadcastSubscribers[actorName].push_back(subscriber);
    m_outputSubscribers[MakeOutputKey(actorName, outputPortName)].push_back(std::move(subscriber));
    m_statsCollector.RegisterPortStats(portStats);
}

void Executor::SetThreadCount(std::size_t threadCount) { m_threadCount = threadCount; }

std::vector<PortRuntimeStats> Executor::GetPortStats() const {
    return m_statsCollector.SnapshotPorts();
}

std::vector<ActorRuntimeStats> Executor::GetActorStats() const {
    return m_statsCollector.SnapshotActors();
}

void Executor::Start() {
    bool expected = false;
    if (!m_started.compare_exchange_strong(expected, true)) {
        return;
    }

    m_stopFlag.store(false, std::memory_order_release);

    std::vector<std::pair<std::string, std::shared_ptr<ActorState>>> actorEntries;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        actorEntries.reserve(m_actorStates.size());
        for (const auto& pair : m_actorStates) {
            actorEntries.push_back(pair);
        }
    }

    auto resolvedThreadCount = ResolveThreadCount(m_threadCount, actorEntries.size());
    if (m_pipelineContext != nullptr) {
        m_pipelineContext->SetExecutorThreadCount(resolvedThreadCount);
    }

    // 根据线程数选择内部调度策略：
    // - 单 worker 更强调公平性，避免一个 actor 长时间霸占唯一线程
    // - 多 worker 更强调吞吐，允许单次多跑几步减少反复入队
    m_schedulingPolicy = CreateSchedulingPolicy(resolvedThreadCount);
    m_threadPool = std::make_unique<ThreadPool>(resolvedThreadCount);
    m_threadPool->Start();
    PrimeActorsOnStart();
}

void Executor::Stop() {
    bool expected = true;
    if (!m_started.compare_exchange_strong(expected, false)) {
        return;
    }

    m_stopFlag.store(true, std::memory_order_release);

    if (m_threadPool) {
        m_threadPool->Stop();
        m_threadPool.reset();
    }
    m_schedulingPolicy.reset();
}

void Executor::PrimeActorsOnStart() {
    std::vector<std::shared_ptr<ActorState>> actorStates;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        actorStates.reserve(m_actorStates.size());
        for (const auto& item : m_actorStates) {
            actorStates.push_back(item.second);
        }
    }

    for (const auto& state : actorStates) {
        if (!state) {
            continue;
        }
        state->taskScheduled.store(false, std::memory_order_release);
        state->pendingRunSignals.store(0, std::memory_order_release);
        if (HasPendingWork(state)) {
            NotifyActorReady(state);
        }
    }
}

void Executor::SubmitActorTask(const std::shared_ptr<ActorState>& state) {
    if (!state || !m_threadPool || !m_started.load(std::memory_order_acquire) ||
        m_stopFlag.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!state->taskScheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    m_threadPool->Submit([this, state]() { RunActorTask(state); });
}

void Executor::NotifyActorReady(const std::shared_ptr<ActorState>& state) {
    if (!state) {
        return;
    }
    state->pendingRunSignals.fetch_add(1, std::memory_order_relaxed);
    SubmitActorTask(state);
}

bool Executor::StatisticsEnabled() const {
    return m_pipelineContext == nullptr || m_pipelineContext->IsStatisticsEnabled();
}

std::uint64_t Executor::ResolveJoinKey(const ActorState& state, const Message& message) const {
    switch (state.runtimeConfig.joinKeyPolicy) {
        case JoinKeyPolicy::Timestamp: return message.GetMetaData().timestamp;
        case JoinKeyPolicy::MessageId:
        default: return message.GetMetaData().messageId;
    }
}

bool Executor::HasPendingWork(const std::shared_ptr<ActorState>& state) const {
    if (!state) {
        return false;
    }
    if (state->inputQueues.empty()) {
        return state->module != nullptr && state->module->GetSourcePolicy() == Module::SourcePolicy::Polling;
    }

    for (const auto& inputQueue : state->inputQueues) {
        if (!inputQueue.queue->IsEmpty()) {
            return true;
        }
    }
    return false;
}

void Executor::RunActorTask(const std::shared_ptr<ActorState>& state) {
    if (!state || !state->module) {
        LOG_ERROR("Invalid actor state for '{}'", state ? state->actorName : std::string("<null>"));
        return;
    }

    // 这个计数表示“本轮执行开始前已经收到多少次 ready 信号”。
    // 先清零，后续若执行过程中又来了新信号，ShouldReschedule 会把它们捞起来。
    state->pendingRunSignals.store(0, std::memory_order_release);

    SchedulingContext schedulingContext;
    schedulingContext.isSourceActor = state->inputQueues.empty();
    schedulingContext.sourcePolicy = state->module->GetSourcePolicy();
    schedulingContext.triggerPolicy = state->module->GetTriggerPolicy();

    const auto maxStepsPerTask =
        m_schedulingPolicy ? m_schedulingPolicy->MaxStepsPerTask(schedulingContext) : std::size_t{1};

    if (schedulingContext.isSourceActor) {
        for (std::size_t step = 0; step < maxStepsPerTask && !m_stopFlag.load(std::memory_order_acquire); ++step) {
            if (!RunSourceStep(state)) {
                break;
            }
        }
    } else {
        auto triggerPolicy = m_schedulingPolicy ? m_schedulingPolicy->ResolveTriggerPolicy(schedulingContext)
                                                : Module::TriggerPolicy::OnAnyInput;

        for (std::size_t step = 0; step < maxStepsPerTask && !m_stopFlag.load(std::memory_order_acquire); ++step) {
            bool didWork =
                triggerPolicy == Module::TriggerPolicy::OnAllInputs ? RunOnAllInputsStep(state) : RunOnAnyInputStep(state);
            if (!didWork) {
                break;
            }
        }
    }

    state->taskScheduled.store(false, std::memory_order_release);
    if (m_stopFlag.load(std::memory_order_acquire)) {
        return;
    }

    schedulingContext.hasPendingSignals = state->pendingRunSignals.load(std::memory_order_acquire) > 0;
    schedulingContext.hasPendingWork = HasPendingWork(state);
    const bool needsReschedule = m_schedulingPolicy ? m_schedulingPolicy->ShouldReschedule(schedulingContext)
                                                    : schedulingContext.hasPendingSignals || schedulingContext.hasPendingWork;
    if (needsReschedule) {
        SubmitActorTask(state);
    }
}

bool Executor::RunSourceStep(const std::shared_ptr<ActorState>& state) {
    // Source actor 没有输入，只是周期性调用 Module::Process，让模块自己决定这轮要不要产出数据。
    std::vector<PortMessage> inputs;
    PortInputsView inputView(inputs);
    PortOutputs outputs;
    state->module->Process(inputView, outputs);
    if (StatisticsEnabled() && state->runtimeStats != nullptr) {
        state->runtimeStats->processCount.fetch_add(1, std::memory_order_relaxed);
    }
    DispatchOutputs(state->actorName, outputs);

    if (outputs.Empty() && state->runtimeConfig.idleWaitUs > 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(state->runtimeConfig.idleWaitUs));
    }
    return true;
}

bool Executor::RunOnAnyInputStep(const std::shared_ptr<ActorState>& state) {
    // OnAnyInput：从任一非空输入端口取一条消息，立刻执行一次 module。
    PortMessage portMessage;
    if (!TryPopAnyInput(state, portMessage)) {
        return false;
    }

    std::vector<PortMessage> inputs;
    inputs.push_back(std::move(portMessage));

    PortInputsView inputView(inputs);
    PortOutputs outputs;
    state->module->Process(inputView, outputs);
    if (StatisticsEnabled() && state->runtimeStats != nullptr) {
        state->runtimeStats->processCount.fetch_add(1, std::memory_order_relaxed);
    }
    DispatchOutputs(state->actorName, outputs);
    return true;
}

bool Executor::RunOnAllInputsStep(const std::shared_ptr<ActorState>& state) {
    // OnAllInputs：先尽量把各输入端口的消息放进 join store，
    // 再尝试取出一组“所有端口都到齐”的 inputs 执行一次 module。
    bool receivedInput = false;
    const bool statisticsEnabled = StatisticsEnabled();

    for (const auto& inputQueue : state->inputQueues) {
        Message message;
        if (!inputQueue.queue->TryPop(message)) {
            continue;
        }

        receivedInput = true;
        if (statisticsEnabled && inputQueue.stats != nullptr) {
            inputQueue.stats->RecordDequeue();
        }
        if (statisticsEnabled && state->runtimeStats != nullptr) {
            state->runtimeStats->inputMessageCount.fetch_add(1, std::memory_order_relaxed);
        }

        state->joinState.Insert(ResolveJoinKey(*state, message), inputQueue.inputPortName, std::move(message));
    }

    const auto currentTimeMs = GetCurrentSystemTimeMs();
    if (StatisticsEnabled() && state->runtimeStats != nullptr) {
        state->runtimeStats->joinTimeoutDropCount.fetch_add(
            state->joinState.EvictExpired(currentTimeMs, state->runtimeConfig.fusionTimeoutMs), std::memory_order_relaxed);
        state->runtimeStats->joinOverflowDropCount.fetch_add(
            state->joinState.EnforceLimit(state->runtimeConfig.maxPendingJoinGroups), std::memory_order_relaxed);
    } else {
        state->joinState.EvictExpired(currentTimeMs, state->runtimeConfig.fusionTimeoutMs);
        state->joinState.EnforceLimit(state->runtimeConfig.maxPendingJoinGroups);
    }

    std::vector<PortMessage> inputs;
    std::vector<std::string> expectedInputPorts;
    expectedInputPorts.reserve(state->inputQueues.size());
    for (const auto& inputQueue : state->inputQueues) {
        expectedInputPorts.push_back(inputQueue.inputPortName);
    }
    if (!state->joinState.TakeCompleteInputs(expectedInputPorts, inputs)) {
        return receivedInput;
    }

    PortInputsView inputView(inputs);
    PortOutputs outputs;
    state->module->Process(inputView, outputs);
    if (statisticsEnabled && state->runtimeStats != nullptr) {
        state->runtimeStats->processCount.fetch_add(1, std::memory_order_relaxed);
    }
    DispatchOutputs(state->actorName, outputs);
    return true;
}

bool Executor::TryPopAnyInput(const std::shared_ptr<ActorState>& state, PortMessage& portMessage) {
    if (state->inputQueues.empty()) {
        return false;
    }

    const std::size_t queueCount = state->inputQueues.size();
    const bool statisticsEnabled = StatisticsEnabled();
    // 轮转扫描输入端口，避免一直优先消费第一个端口造成偏斜。
    for (std::size_t offset = 0; offset < queueCount; ++offset) {
        auto index = (state->nextInputIndex + offset) % queueCount;
        auto& inputQueue = state->inputQueues[index];

        Message message;
        if (inputQueue.queue->TryPop(message)) {
            state->nextInputIndex = (index + 1) % queueCount;
            if (statisticsEnabled && inputQueue.stats != nullptr) {
                inputQueue.stats->RecordDequeue();
            }
            if (statisticsEnabled && state->runtimeStats != nullptr) {
                state->runtimeStats->inputMessageCount.fetch_add(1, std::memory_order_relaxed);
            }
            portMessage.port = inputQueue.inputPortName;
            portMessage.message = std::move(message);
            return true;
        }
    }

    return false;
}

void Executor::DispatchOutputs(const std::string& actorName, PortOutputs& outputs) {
    // 先记统计，再真正把 outputs 分发到下游。
    if (StatisticsEnabled()) {
        auto actorIt = m_actorStates.find(actorName);
        if (actorIt != m_actorStates.end() && actorIt->second->runtimeStats != nullptr) {
            actorIt->second->runtimeStats->emittedBroadcastCount.fetch_add(
                static_cast<std::uint64_t>(outputs.m_broadcasts.size()), std::memory_order_relaxed);
            actorIt->second->runtimeStats->emittedRouteCount.fetch_add(static_cast<std::uint64_t>(outputs.m_routes.size()),
                                                                       std::memory_order_relaxed);
        }
    }

    for (const auto& command : outputs.m_broadcasts) {
        Emit(actorName, command.message, command.blocking);
    }

    for (const auto& command : outputs.m_routes) {
        Route(actorName, command.portMessage.port, command.portMessage.message, command.blocking);
    }
}

void Executor::Emit(const std::string& actorName, const Message& message, bool blocking) {
    auto it = m_broadcastSubscribers.find(actorName);
    if (it == m_broadcastSubscribers.end()) {
        return;
    }

    for (const auto& subscriber : it->second) {
        DispatchToSubscriber(subscriber, message, blocking);
    }
}

void Executor::Route(const std::string& actorName, const std::string& outputPortName, const Message& message, bool blocking) {
    auto it = m_outputSubscribers.find(MakeOutputKey(actorName, outputPortName));
    if (it == m_outputSubscribers.end()) {
        return;
    }

    for (const auto& subscriber : it->second) {
        DispatchToSubscriber(subscriber, message, blocking);
    }
}

void Executor::DispatchToSubscriber(const OutputSubscriber& subscriber, const Message& message, bool blocking) {
    const bool statisticsEnabled = StatisticsEnabled();
    if (statisticsEnabled && subscriber.stats != nullptr) {
        subscriber.stats->RecordPushAttempt(blocking);
    }

    if (blocking) {
        // blocking 模式下直接等队列接受或关闭。
        auto status = subscriber.queue->PushWithStatus(message);
        if (status == MessageQueue::PushStatus::Success) {
            if (statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushAccepted(1, 0);
            }
            NotifyActorReady(subscriber.dstActorState);
        } else if (statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushRejected();
        }
        return;
    }

    auto queueFullPolicy = QueueFullPolicy::DropTail;
    if (m_pipelineContext != nullptr) {
        queueFullPolicy = m_pipelineContext->GetConfig().nonBlockingQueueFullPolicy;
    }

    if (queueFullPolicy == QueueFullPolicy::DropHead) {
        // DropHead：队列满时丢最老的，尽量保留最新数据。
        auto result = subscriber.queue->TryPushDropHead(message);
        if (result.status == MessageQueue::PushStatus::Success) {
            if (statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushAccepted(1, result.droppedCount);
            }
            NotifyActorReady(subscriber.dstActorState);
        } else if (result.status == MessageQueue::PushStatus::Shutdown) {
            if (statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushRejected();
            }
        } else if (statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushDropped(1);
        }
        return;
    }

    // 默认 DropTail：队列满时丢当前这条新消息，保留队列中的旧数据。
    auto status = subscriber.queue->TryPushWithStatus(message);
    if (status == MessageQueue::PushStatus::Success) {
        if (statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushAccepted(1, 0);
        }
        NotifyActorReady(subscriber.dstActorState);
    } else if (status == MessageQueue::PushStatus::Shutdown) {
        if (statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushRejected();
        }
    } else if (statisticsEnabled && subscriber.stats != nullptr) {
        subscriber.stats->RecordPushDropped(1);
    }
}

}} // namespace nexusflow::executor
