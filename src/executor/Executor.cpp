#include "executor/Executor.hpp"

#include "utils/logging.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
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

constexpr std::size_t kMaxInputStepsPerTask = 64;
constexpr std::size_t kMaxSourceStepsPerTask = 1;

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
            std::lock_guard<std::mutex> pendingLock(state->pendingJoinGroupsMutex);
            return static_cast<std::uint64_t>(state->pendingJoinGroups.size());
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

    state->pendingRunSignals.store(0, std::memory_order_release);

    if (state->inputQueues.empty()) {
        for (std::size_t step = 0; step < kMaxSourceStepsPerTask && !m_stopFlag.load(std::memory_order_acquire); ++step) {
            if (!RunSourceStep(state)) {
                break;
            }
        }
    } else {
        auto triggerPolicy = state->module->GetTriggerPolicy();
        if (triggerPolicy == Module::TriggerPolicy::Auto) {
            triggerPolicy = Module::TriggerPolicy::OnAnyInput;
        }

        for (std::size_t step = 0; step < kMaxInputStepsPerTask && !m_stopFlag.load(std::memory_order_acquire); ++step) {
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

    bool needsReschedule =
        state->inputQueues.empty() && state->module->GetSourcePolicy() == Module::SourcePolicy::Polling;
    if (!needsReschedule && state->pendingRunSignals.load(std::memory_order_acquire) > 0) {
        needsReschedule = true;
    }
    if (!needsReschedule && HasPendingWork(state)) {
        needsReschedule = true;
    }
    if (needsReschedule) {
        SubmitActorTask(state);
    }
}

bool Executor::RunSourceStep(const std::shared_ptr<ActorState>& state) {
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

        const auto messageId = ResolveJoinKey(*state, message);
        std::lock_guard<std::mutex> lock(state->pendingJoinGroupsMutex);
        auto groupIt = state->pendingJoinGroups.find(messageId);
        if (groupIt == state->pendingJoinGroups.end()) {
            auto insertResult = state->pendingJoinGroups.emplace(messageId, ActorState::PendingJoinGroup{});
            groupIt = insertResult.first;
        }

        auto& group = groupIt->second;
        const auto messageTimestamp = message.GetMetaData().timestamp;
        if (group.oldestTimestampMs == 0 || messageTimestamp < group.oldestTimestampMs) {
            group.oldestTimestampMs = messageTimestamp;
        }
        group.messages[inputQueue.inputPortName] = std::move(message);
    }

    const auto currentTimeMs = GetCurrentSystemTimeMs();
    CleanupExpiredJoinGroups(state, currentTimeMs);
    EnforcePendingJoinGroupLimit(state);

    std::vector<PortMessage> inputs;
    if (!TryTakeCompleteJoinInputs(state, inputs)) {
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

bool Executor::TryTakeCompleteJoinInputs(const std::shared_ptr<ActorState>& state, std::vector<PortMessage>& inputs) {
    if (!state) {
        return false;
    }

    std::lock_guard<std::mutex> lock(state->pendingJoinGroupsMutex);
    const auto expectedInputCount = state->inputQueues.size();
    for (auto groupIt = state->pendingJoinGroups.begin(); groupIt != state->pendingJoinGroups.end(); ++groupIt) {
        auto& group = groupIt->second;
        if (group.messages.size() != expectedInputCount) {
            continue;
        }

        inputs.clear();
        inputs.reserve(expectedInputCount);
        for (const auto& inputQueue : state->inputQueues) {
            auto messageIt = group.messages.find(inputQueue.inputPortName);
            if (messageIt != group.messages.end()) {
                inputs.push_back(PortMessage{inputQueue.inputPortName, std::move(messageIt->second)});
            }
        }
        state->pendingJoinGroups.erase(groupIt);
        return true;
    }

    return false;
}

void Executor::CleanupExpiredJoinGroups(const std::shared_ptr<ActorState>& state, std::uint64_t currentTimeMs) {
    if (!state) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->pendingJoinGroupsMutex);
    for (auto groupIt = state->pendingJoinGroups.begin(); groupIt != state->pendingJoinGroups.end();) {
        const auto oldestTimestampMs = groupIt->second.oldestTimestampMs;
        if (oldestTimestampMs + state->runtimeConfig.fusionTimeoutMs < currentTimeMs) {
            if (StatisticsEnabled() && state->runtimeStats != nullptr) {
                state->runtimeStats->joinTimeoutDropCount.fetch_add(1, std::memory_order_relaxed);
            }
            groupIt = state->pendingJoinGroups.erase(groupIt);
        } else {
            ++groupIt;
        }
    }
}

void Executor::EnforcePendingJoinGroupLimit(const std::shared_ptr<ActorState>& state) {
    if (!state || state->runtimeConfig.maxPendingJoinGroups == 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(state->pendingJoinGroupsMutex);
    while (state->pendingJoinGroups.size() > state->runtimeConfig.maxPendingJoinGroups) {
        auto oldestIt = state->pendingJoinGroups.end();
        auto oldestTimestamp = std::numeric_limits<std::uint64_t>::max();
        for (auto it = state->pendingJoinGroups.begin(); it != state->pendingJoinGroups.end(); ++it) {
            if (it->second.oldestTimestampMs < oldestTimestamp) {
                oldestTimestamp = it->second.oldestTimestampMs;
                oldestIt = it;
            }
        }

        if (oldestIt == state->pendingJoinGroups.end()) {
            break;
        }
        if (StatisticsEnabled() && state->runtimeStats != nullptr) {
            state->runtimeStats->joinOverflowDropCount.fetch_add(1, std::memory_order_relaxed);
        }
        state->pendingJoinGroups.erase(oldestIt);
    }
}

void Executor::DispatchOutputs(const std::string& actorName, PortOutputs& outputs) {
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
