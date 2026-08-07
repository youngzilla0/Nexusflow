#include "executor/Executor.hpp"

#include "base/Graph.hpp"
#include "utils/logging.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace nexusflow { namespace executor {

namespace {

/**
 * @brief 返回当前系统时间戳，单位毫秒。
 * @return 当前系统时间戳。
 */
uint64_t GetCurrentSystemTimeMs() {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

/**
 * @brief 解析 Executor 实际使用的线程数。
 * @param configuredThreadCount 用户配置的线程数，0 表示自动推导。
 * @param actorCount 当前 Pipeline 中的 actor 数量。
 * @return 最终使用的线程数。
 */
std::size_t ResolveThreadCount(std::size_t configuredThreadCount, std::size_t actorCount) {
    (void)actorCount;
    if (configuredThreadCount > 0) {
        return configuredThreadCount;
    }

    auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    if (hardwareThreads == 0) hardwareThreads = 1;
    return std::max<std::size_t>(hardwareThreads, 1);
}

/**
 * @brief 在未配置调度策略时构造默认执行计划。
 * @param context 当前 actor 的调度上下文。
 * @return 最基础的单轮执行计划。
 */
TaskExecutionPlan MakeFallbackExecutionPlan(const SchedulingContext& context) {
    TaskExecutionPlan plan;
    if (context.isSourceActor) {
        plan.executionMode = TaskExecutionMode::Source;
        plan.maxStepsPerTask = 1;
        return plan;
    }

    const auto triggerPolicy =
        context.triggerPolicy == Module::TriggerPolicy::Auto ? Module::TriggerPolicy::OnAnyInput : context.triggerPolicy;
    plan.executionMode =
        triggerPolicy == Module::TriggerPolicy::OnAllInputs ? TaskExecutionMode::OnAllInputs : TaskExecutionMode::OnAnyInput;
    plan.maxStepsPerTask = 1;
    return plan;
}

} // namespace

/**
 * @brief 构造 Executor。
 * @param pipelineContext 所属 Pipeline 的共享上下文。
 */
Executor::Executor(const std::shared_ptr<PipelineContext>& pipelineContext)
    : m_statsCollector(pipelineContext == nullptr || pipelineContext->IsStatisticsEnabled()),
      m_portRouter(
          pipelineContext == nullptr || pipelineContext->IsStatisticsEnabled(),
          [this]() {
              if (m_pipelineContext != nullptr) {
                  return m_pipelineContext->GetConfig().nonBlockingQueueFullPolicy;
              }
              return QueueFullPolicy::DropTail;
          },
          [this](const std::string& nodeName) {
              NotifyNodeReady(m_nodeRegistry.Find(nodeName));
          },
          [](const PipelineMessageEvent&) {}),
      m_pipelineContext(pipelineContext) {}

/**
 * @brief 析构 Executor。
 */
Executor::~Executor() { Stop(); }

/**
 * @brief 注册一个可调度模块。
 * @param actorName actor 名称。
 * @param module 对应的模块实例。
 * @param runtimeConfig actor 对应的运行时配置。
 */
void Executor::RegisterNode(const std::string& actorName, const std::shared_ptr<Module>& module,
                            const PipelineConfig& runtimeConfig,
                            bool isSinkNode) {
    m_nodeRegistry.RegisterNode(actorName, module, runtimeConfig, m_statsCollector, isSinkNode);
}

/**
 * @brief 为 actor 注册一个输入队列绑定。
 * @param actorName 目标 actor 名称。
 * @param inputPortName 目标输入端口名称。
 * @param queue 输入消息队列。
 * @param stats 对应边的统计状态。
 */
void Executor::AddInputQueue(const std::string& actorName, const std::string& inputPortName, ViewPtr<MessageQueue> queue,
                             const PortStatsStatePtr& stats) {
    m_nodeRegistry.AddInputQueue(actorName, inputPortName, queue, stats);
}

/**
 * @brief 为 actor 注册一个输出订阅边。
 * @param actorName 源 actor 名称。
 * @param outputPortName 源输出端口名称。
 * @param dstActorName 目标 actor 名称。
 * @param dstInputPortName 目标输入端口名称。
 * @param queue 源到目标之间的消息队列。
 * @param stats 对应边的统计状态。
 */
void Executor::AddOutputQueue(const std::string& actorName, const std::string& outputPortName, const std::string& dstActorName,
                              const std::string& dstInputPortName, ViewPtr<MessageQueue> queue,
                              const PortStatsStatePtr& stats) {
    auto portStats = StatisticsEnabled() ? stats : nullptr;
    if (StatisticsEnabled() && portStats == nullptr) {
        portStats = std::make_shared<PortStatsState>(actorName, outputPortName, dstActorName, dstInputPortName);
    }

    if (m_nodeRegistry.Find(dstActorName) == nullptr) {
        throw std::invalid_argument("Unknown actor " + dstActorName);
    }

    m_portRouter.AddOutputQueue(actorName, outputPortName, dstActorName, dstInputPortName, queue, portStats);
    m_statsCollector.RegisterPortStats(portStats);
}

/** @brief 设置 Executor 的线程数配置。 */
void Executor::SetThreadCount(std::size_t threadCount) { m_threadCount = threadCount; }

/** @brief 设置消息投递异常事件回调。 */
void Executor::SetMessageEventCallback(PortRouter::MessageEventCallback callback) {
    m_portRouter.SetMessageEventCallback(std::move(callback));
}

/** @brief 生成全部边级统计快照。 */
std::vector<PortStats> Executor::GetPortStats() const {
    return m_statsCollector.SnapshotPorts();
}

/** @brief 生成全部节点级统计快照。 */
std::vector<NodeStats> Executor::GetNodeStats() const {
    return m_statsCollector.SnapshotNodes();
}

PipelineSummaryStats Executor::GetSummaryStats() const {
    PipelineSummaryStats summary;
    const auto ports = m_statsCollector.SnapshotPorts();
    for (const auto& port : ports) {
        summary.totalPushAttempts += port.pushAttempts;
        summary.totalEnqueueCount += port.enqueueCount;
        summary.totalDropCount += port.dropCount;
        summary.totalRejectCount += port.rejectCount;
    }

    summary.sinkReceiveCount = m_statsCollector.SnapshotSinkReceiveCount();

    auto latencySamples = m_statsCollector.SnapshotSinkLatencies();
    summary.latencySampleCount = static_cast<std::uint64_t>(latencySamples.size());
    if (!latencySamples.empty()) {
        std::sort(latencySamples.begin(), latencySamples.end());
        const auto p50Index = (latencySamples.size() - 1) * 50 / 100;
        const auto p99Index = (latencySamples.size() - 1) * 99 / 100;
        summary.latencyP50Ms = latencySamples[p50Index];
        summary.latencyP99Ms = latencySamples[p99Index];
        summary.latencyMaxMs = latencySamples.back();
    }

    const auto totalOutcomes = summary.totalEnqueueCount + summary.totalDropCount + summary.totalRejectCount;
    if (totalOutcomes > 0) {
        summary.dropRate =
            static_cast<double>(summary.totalDropCount) / static_cast<double>(totalOutcomes);
        summary.rejectRate =
            static_cast<double>(summary.totalRejectCount) / static_cast<double>(totalOutcomes);
    }

    return summary;
}

/**
 * @brief 启动 Executor。
 *
 * 该函数负责解析线程数、创建线程池、选择调度策略，并提交初始可运行 actor。
 */
void Executor::Start() {
    bool expected = false;
    if (!m_started.compare_exchange_strong(expected, true)) {
        return;
    }

    m_stopFlag.store(false, std::memory_order_release);

    auto resolvedThreadCount = ResolveThreadCount(m_threadCount, m_nodeRegistry.Size());
    if (m_pipelineContext != nullptr) {
        m_pipelineContext->SetExecutorThreadCount(resolvedThreadCount);
    }

    m_schedulingPolicy = CreateSchedulingPolicy(resolvedThreadCount);
    m_threadPool = std::make_unique<ThreadPool>(resolvedThreadCount);
    m_threadPool->Start();
    PrimeNodesOnStart();
}

/**
 * @brief 停止 Executor。
 */
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

/**
 * @brief 在启动阶段提交所有应立即执行的 actor。
 */
void Executor::PrimeNodesOnStart() {
    auto nodeStates = m_nodeRegistry.SnapshotStates();

    for (const auto& state : nodeStates) {
        if (!state) {
            continue;
        }
        state->taskScheduled.store(false, std::memory_order_release);
        state->pendingRunSignals.store(0, std::memory_order_release);
        const auto schedulingContext = BuildSchedulingContext(*state);
        const bool hasPendingWork = HasPendingWork(state);
        const bool shouldPrime =
            m_schedulingPolicy ? m_schedulingPolicy->ShouldPrimeActorOnStart(schedulingContext, hasPendingWork)
                               : hasPendingWork;
        if (shouldPrime) {
            NotifyNodeReady(state);
        }
    }
}

/**
 * @brief 将 actor 提交给线程池执行。
 * @param state 目标 actor 的调度状态。
 */
void Executor::SubmitNodeTask(const NodeStateRegistry::NodeStatePtr& state) {
    if (!state || !m_threadPool || !m_started.load(std::memory_order_acquire) ||
        m_stopFlag.load(std::memory_order_acquire)) {
        return;
    }

    bool expected = false;
    if (!state->taskScheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return;
    }

    m_threadPool->Submit([this, state]() { RunNodeTask(state); });
}

/**
 * @brief 标记 actor 就绪，并在必要时触发任务提交。
 * @param state 目标 actor 的调度状态。
 */
void Executor::NotifyNodeReady(const NodeStateRegistry::NodeStatePtr& state) {
    if (!state) {
        return;
    }
    state->pendingRunSignals.fetch_add(1, std::memory_order_relaxed);
    SubmitNodeTask(state);
}

/** @brief 返回统计是否启用。 */
bool Executor::StatisticsEnabled() const {
    return m_pipelineContext == nullptr || m_pipelineContext->IsStatisticsEnabled();
}

/**
 * @brief 构造调度策略使用的上下文对象。
 * @param state 当前 actor 的调度状态。
 * @return 当前 actor 的调度上下文。
 */
SchedulingContext Executor::BuildSchedulingContext(const NodeStateRegistry::NodeState& state) const {
    SchedulingContext context;
    context.isSourceActor = state.inputQueues.empty();
    context.sourcePolicy = state.module != nullptr ? state.module->GetSourcePolicy() : Module::SourcePolicy::Polling;
    context.triggerPolicy = state.module != nullptr ? state.module->GetTriggerPolicy() : Module::TriggerPolicy::Auto;
    context.idleWaitUs = state.runtimeConfig.idleWaitUs;
    if (m_pipelineContext) {
        context.topologyInfo = &m_pipelineContext->GetTopologyInfo();
        context.branchCount = context.topologyInfo->branchNodes.size();
        context.joinCount = context.topologyInfo->joinNodes.size();
        context.forkJoinGroupCount = context.topologyInfo->forkJoinGroups.size();

        for (const auto& nodeDegree : context.topologyInfo->nodeDegrees) {
            if (!nodeDegree.node || nodeDegree.node->name != state.nodeName) {
                continue;
            }
            context.localBranchFanOut = nodeDegree.outgoingCount;
            context.localJoinFanIn = nodeDegree.incomingCount;
            context.isBranchActor = nodeDegree.outgoingCount >= 2;
            context.isJoinActor = nodeDegree.incomingCount >= 2;
            break;
        }

        for (const auto& group : context.topologyInfo->forkJoinGroups) {
            if ((group.forkNode && group.forkNode->name == state.nodeName) ||
                (group.joinNode && group.joinNode->name == state.nodeName)) {
                context.isForkJoinActor = true;
            }

            if (group.forkNode && group.forkNode->name == state.nodeName) {
                context.localBranchFanOut = std::max<std::size_t>(context.localBranchFanOut, group.paths.size());
            }
            if (group.joinNode && group.joinNode->name == state.nodeName) {
                context.localJoinFanIn = std::max<std::size_t>(context.localJoinFanIn, group.paths.size());
            }
        }
    }
    return context;
}

/**
 * @brief 根据 actor 配置解析消息 join key。
 * @param state 当前 actor 的调度状态。
 * @param message 待解析消息。
 * @return 当前消息对应的 join key。
 */
std::uint64_t Executor::ResolveJoinKey(const NodeStateRegistry::NodeState& state, const Message& message) const {
    switch (state.runtimeConfig.joinKeyPolicy) {
        case JoinKeyPolicy::Timestamp: return message.GetMetaData().timestamp;
        case JoinKeyPolicy::MessageId:
        default: return message.GetMetaData().messageId;
    }
}

/**
 * @brief 判断当前 actor 是否仍有待处理工作。
 * @param state 当前 actor 的调度状态。
 * @return 若仍有工作可继续处理，则返回 true。
 */
bool Executor::HasPendingWork(const NodeStateRegistry::NodeStatePtr& state) const {
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

/**
 * @brief 执行 actor 的一轮任务。
 * @param state 当前 actor 的调度状态。
 *
 * 该函数负责：
 * - 生成单轮执行计划
 * - 按步数预算执行 source / OnAnyInput / OnAllInputs 逻辑
 * - 汇总执行反馈
 * - 依据调度策略决定是否续调度
 */
void Executor::RunNodeTask(const NodeStateRegistry::NodeStatePtr& state) {
    if (!state || !state->module) {
        LOG_ERROR("Invalid node state for '{}'", state ? state->nodeName : std::string("<null>"));
        return;
    }

    state->pendingRunSignals.store(0, std::memory_order_release);

    const auto schedulingContext = BuildSchedulingContext(*state);
    const auto executionPlan =
        m_schedulingPolicy ? m_schedulingPolicy->Plan(schedulingContext) : MakeFallbackExecutionPlan(schedulingContext);

    SchedulingFeedback feedback;

    for (std::size_t step = 0; step < executionPlan.maxStepsPerTask && !m_stopFlag.load(std::memory_order_acquire); ++step) {
        StepResult stepResult;
        switch (executionPlan.executionMode) {
            case TaskExecutionMode::Source: stepResult = RunSourceStep(state); break;
            case TaskExecutionMode::OnAllInputs: stepResult = RunOnAllInputsStep(state); break;
            case TaskExecutionMode::OnAnyInput:
            default: stepResult = RunOnAnyInputStep(state); break;
        }

        if (!stepResult.madeProgress) {
            feedback.stoppedByNoWork = true;
            break;
        }

        feedback.completedSteps += 1;
        feedback.madeProgress = true;
        feedback.emittedOutputs = feedback.emittedOutputs || stepResult.emittedOutputs;
    }

    if (!m_stopFlag.load(std::memory_order_acquire)) {
        const auto idleBackoff = m_schedulingPolicy ? m_schedulingPolicy->IdleBackoff(schedulingContext, feedback)
                                                    : std::chrono::microseconds(schedulingContext.idleWaitUs);
        if (idleBackoff.count() > 0) {
            std::this_thread::sleep_for(idleBackoff);
        }
    }

    state->taskScheduled.store(false, std::memory_order_release);
    if (m_stopFlag.load(std::memory_order_acquire)) {
        return;
    }

    feedback.hasPendingSignals = state->pendingRunSignals.load(std::memory_order_acquire) > 0;
    feedback.hasPendingWork = HasPendingWork(state);
    const bool needsReschedule = m_schedulingPolicy ? m_schedulingPolicy->ShouldReschedule(schedulingContext, feedback)
                                                    : feedback.hasPendingSignals || feedback.hasPendingWork;
    if (needsReschedule) {
        SubmitNodeTask(state);
    }
}

/**
 * @brief 执行 source actor 的单个 step。
 * @param state 当前 actor 的调度状态。
 * @return 当前 step 的执行结果。
 */
Executor::StepResult Executor::RunSourceStep(const NodeStateRegistry::NodeStatePtr& state) {
    std::vector<PortMessage> inputs;
    PortInputsView inputView(inputs);
    PortOutputs outputs;
    state->module->Process(inputView, outputs);
    const bool emittedOutputs = !outputs.Empty();
    if (StatisticsEnabled() && state->stats != nullptr) {
        state->stats->processCount.fetch_add(1, std::memory_order_relaxed);
    }
    DispatchOutputs(state->nodeName, outputs);
    return StepResult{true, emittedOutputs};
}

/**
 * @brief 执行 OnAnyInput actor 的单个 step。
 * @param state 当前 actor 的调度状态。
 * @return 当前 step 的执行结果。
 */
Executor::StepResult Executor::RunOnAnyInputStep(const NodeStateRegistry::NodeStatePtr& state) {
    PortMessage portMessage;
    if (!TryPopAnyInput(state, portMessage)) {
        return StepResult{};
    }

    std::vector<PortMessage> inputs;
    inputs.push_back(std::move(portMessage));
    RecordSinkLatencyIfNeeded(state, inputs.front().message);

    PortInputsView inputView(inputs);
    PortOutputs outputs;
    state->module->Process(inputView, outputs);
    const bool emittedOutputs = !outputs.Empty();
    if (StatisticsEnabled() && state->stats != nullptr) {
        state->stats->processCount.fetch_add(1, std::memory_order_relaxed);
    }
    DispatchOutputs(state->nodeName, outputs);
    return StepResult{true, emittedOutputs};
}

/**
 * @brief 执行 OnAllInputs actor 的单个 step。
 * @param state 当前 actor 的调度状态。
 * @return 当前 step 的执行结果。
 */
Executor::StepResult Executor::RunOnAllInputsStep(const NodeStateRegistry::NodeStatePtr& state) {
    bool receivedInput = false;
    const bool statisticsEnabled = StatisticsEnabled();
    std::vector<std::string> expectedInputPorts;
    expectedInputPorts.reserve(state->inputQueues.size());

    for (const auto& inputQueue : state->inputQueues) {
        expectedInputPorts.push_back(inputQueue.inputPortName);

        Message message;
        if (!inputQueue.queue->TryPop(message)) {
            continue;
        }

        receivedInput = true;
        if (statisticsEnabled && inputQueue.stats != nullptr) {
            inputQueue.stats->RecordDequeue();
        }
        if (statisticsEnabled && state->stats != nullptr) {
            state->stats->inputMessageCount.fetch_add(1, std::memory_order_relaxed);
        }

        state->joinState.Insert(ResolveJoinKey(*state, message), inputQueue.inputPortName, std::move(message));
    }

    const auto currentTimeMs = GetCurrentSystemTimeMs();
    std::vector<PortMessage> inputs;
    const auto sweepResult = state->joinState.SweepAndTakeCompleteInputs(
        expectedInputPorts, currentTimeMs, state->runtimeConfig.fusionTimeoutMs,
        state->runtimeConfig.maxPendingJoinGroups, inputs);

    if (statisticsEnabled && state->stats != nullptr) {
        state->stats->joinTimeoutDropCount.fetch_add(sweepResult.expiredGroupCount, std::memory_order_relaxed);
        state->stats->joinOverflowDropCount.fetch_add(sweepResult.overflowGroupCount, std::memory_order_relaxed);
    }

    if (!sweepResult.tookCompleteGroup) {
        return StepResult{receivedInput, false};
    }

    RecordSinkLatencyIfNeeded(state, inputs);

    PortInputsView inputView(inputs);
    PortOutputs outputs;
    state->module->Process(inputView, outputs);
    const bool emittedOutputs = !outputs.Empty();
    if (statisticsEnabled && state->stats != nullptr) {
        state->stats->processCount.fetch_add(1, std::memory_order_relaxed);
    }
    DispatchOutputs(state->nodeName, outputs);
    return StepResult{true, emittedOutputs};
}

/**
 * @brief 从任一输入端口提取一条消息。
 * @param state 当前 actor 的调度状态。
 * @param portMessage 输出参数，用于接收提取到的端口消息。
 * @return 成功提取一条消息时返回 true。
 */
bool Executor::TryPopAnyInput(const NodeStateRegistry::NodeStatePtr& state, PortMessage& portMessage) {
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
            if (statisticsEnabled && state->stats != nullptr) {
                state->stats->inputMessageCount.fetch_add(1, std::memory_order_relaxed);
            }
            portMessage.port = inputQueue.inputPortName;
            portMessage.message = std::move(message);
            return true;
        }
    }

    return false;
}

void Executor::RecordSinkLatencyIfNeeded(const NodeStateRegistry::NodeStatePtr& state, const Message& message) {
    if (!StatisticsEnabled() || !state || !state->isSinkNode) {
        return;
    }

    const auto nowMs = GetCurrentSystemTimeMs();
    const auto messageTimestampMs = message.GetMetaData().timestamp;
    const auto latencyMs = nowMs >= messageTimestampMs ? nowMs - messageTimestampMs : 0;
    m_statsCollector.RecordSinkLatency(state->nodeName, latencyMs);
}

void Executor::RecordSinkLatencyIfNeeded(const NodeStateRegistry::NodeStatePtr& state,
                                         const std::vector<PortMessage>& inputs) {
    if (!StatisticsEnabled() || !state || !state->isSinkNode || inputs.empty()) {
        return;
    }

    const auto nowMs = GetCurrentSystemTimeMs();
    std::uint64_t maxLatencyMs = 0;
    for (const auto& input : inputs) {
        const auto messageTimestampMs = input.message.GetMetaData().timestamp;
        const auto latencyMs = nowMs >= messageTimestampMs ? nowMs - messageTimestampMs : 0;
        if (latencyMs > maxLatencyMs) {
            maxLatencyMs = latencyMs;
        }
    }
    m_statsCollector.RecordSinkLatency(state->nodeName, maxLatencyMs);
}

/**
 * @brief 分发模块本轮产生的全部输出。
 * @param actorName 源 actor 名称。
 * @param outputs 模块产生的输出集合。
 */
void Executor::DispatchOutputs(const std::string& actorName, PortOutputs& outputs) {
    if (StatisticsEnabled()) {
        auto nodeState = m_nodeRegistry.Find(actorName);
        if (nodeState != nullptr && nodeState->stats != nullptr) {
            nodeState->stats->emittedBroadcastCount.fetch_add(
                static_cast<std::uint64_t>(outputs.m_broadcasts.size()), std::memory_order_relaxed);
            nodeState->stats->emittedRouteCount.fetch_add(static_cast<std::uint64_t>(outputs.m_routes.size()),
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

/**
 * @brief 将一条广播消息分发给全部下游订阅者。
 * @param actorName 源 actor 名称。
 * @param message 待分发消息。
 * @param blocking 是否采用阻塞推送。
 */
void Executor::Emit(const std::string& actorName, const Message& message, bool blocking) {
    m_portRouter.Emit(actorName, message, blocking);
}

/**
 * @brief 将一条路由消息分发给指定输出端口的全部订阅者。
 * @param actorName 源 actor 名称。
 * @param outputPortName 源输出端口名称。
 * @param message 待分发消息。
 * @param blocking 是否采用阻塞推送。
 */
void Executor::Route(const std::string& actorName, const std::string& outputPortName, const Message& message, bool blocking) {
    m_portRouter.Route(actorName, outputPortName, message, blocking);
}

}} // namespace nexusflow::executor
