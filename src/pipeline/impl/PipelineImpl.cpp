#include "PipelineImpl.hpp"
#include "base/Graph.hpp"
#include "utils/logging.hpp"
#include <nexusflow/ModuleFactory.hpp>

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <thread>

namespace nexusflow {

namespace {

/**
 * @brief 解析 Executor 实际使用的线程数。
 * @param configuredThreadCount 用户配置的线程数，0 表示自动推导。
 * @param actorCount 当前 Pipeline 中的 actor 数量。
 * @return 最终使用的线程数。
 */
std::size_t ResolveExecutorThreadCount(std::size_t configuredThreadCount, std::size_t actorCount) {
    (void)actorCount;
    if (configuredThreadCount > 0) {
        return configuredThreadCount;
    }

    auto hardwareThreads = static_cast<std::size_t>(std::thread::hardware_concurrency());
    if (hardwareThreads == 0) {
        hardwareThreads = 1;
    }
    return std::max<std::size_t>(hardwareThreads, 1);
}

/**
 * @brief 根据图层节点创建对应的 Module 实例。
 * @param node Graph 层的节点对象。
 * @return 该节点对应的模块实例。
 *
 * 如果图节点本身就是 Module 实例描述，则直接复用；
 * 如果图节点描述的是 class 名，则通过 ModuleFactory 物化。
 */
std::shared_ptr<Module> CreateModuleForGraphNode(const std::shared_ptr<GraphNode>& node) {
    if (!node) {
        throw std::runtime_error("Cannot create a module from a null graph node.");
    }

    switch (node->GetKind()) {
        case GraphNodeKind::Module: {
            auto& graphModuleNode = static_cast<GraphModuleNode&>(*node);
            if (graphModuleNode.source == ModuleSource::Instance) {
                return graphModuleNode.modulePtr;
            }

            auto& moduleFactory = ModuleFactory::GetInstance();
            return moduleFactory.CreateModule(
                ModuleBuildContext{graphModuleNode.moduleClassName, graphModuleNode.name, graphModuleNode.config});
        }
        case GraphNodeKind::Generic:
        default:
            throw std::runtime_error("Graph node '" + node->name +
                                     "' only describes topology and cannot materialize a Module.");
    }
}

/**
 * @brief 根据模块与运行时配置生成节点的运行时画像。
 * @param module 节点对应的模块实例。
 * @param runtimeConfig 该节点的运行时配置。
 * @param statisticsEnabled 当前 Pipeline 是否启用统计。
 * @param isSourceNode 是否为 source 节点。
 * @param isSinkNode 是否为 sink 节点。
 * @return 节点运行时画像。
 */
ExecutionPlan::PlannedNodeRuntimeProfile BuildNodeRuntimeProfile(const std::shared_ptr<Module>& module,
                                                                 const PipelineConfig& runtimeConfig,
                                                                 bool statisticsEnabled,
                                                                 bool isSourceNode,
                                                                 bool isSinkNode) {
    if (!module) {
        throw std::runtime_error("Cannot build a runtime profile for a null module.");
    }

    ExecutionPlan::PlannedNodeRuntimeProfile profile;
    profile.runtimeConfig = runtimeConfig;
    profile.statisticsEnabled = statisticsEnabled;
    profile.triggerPolicy = module->GetTriggerPolicy();
    profile.sourcePolicy = module->GetSourcePolicy();
    profile.isSourceNode = isSourceNode;
    profile.isSinkNode = isSinkNode;
    profile.joinKeyPolicy = runtimeConfig.joinKeyPolicy;
    profile.idleWaitUs = runtimeConfig.idleWaitUs;
    profile.fusionTimeoutMs = runtimeConfig.fusionTimeoutMs;
    profile.maxPendingJoinGroups = runtimeConfig.maxPendingJoinGroups;
    return profile;
}

} // namespace

std::string Pipeline::Impl::GetPipelineName() const {
    if (pipelineContext != nullptr) {
        return pipelineContext->GetPipelineName();
    }
    if (graph) {
        return graph->GetName();
    }
    return "";
}

/**
 * @brief 校验 Pipeline 图是否可以进入运行阶段。
 * @return 校验结果。
 */
ErrorCode Pipeline::Impl::ValidateGraph() const {
    if (!graph) {
        LOG_ERROR("Pipeline graph is null.");
        return ErrorCode::UNINITIALIZED_ERROR;
    }

    if (graph->GetName().empty()) {
        LOG_ERROR("Pipeline graph name is empty.");
        return ErrorCode::FAILURE;
    }

    if (graph->IsEmpty()) {
        LOG_ERROR("Pipeline graph '{}' is empty or incomplete.", graph->GetName());
        return ErrorCode::FAILURE;
    }

    if (graph->HasCycle()) {
        LOG_ERROR("Pipeline graph '{}' contains a cycle.", graph->GetName());
        return ErrorCode::FAILURE;
    }

    return ErrorCode::SUCCESS;
}

/**
 * @brief 把图结构转换成执行计划。
 * @return 执行计划。
 *
 * 这里会同时推导 source / sink 节点、队列绑定和统计开关。
 */
ExecutionPlan Pipeline::Impl::BuildExecutionPlan() const {
    ExecutionPlan plan;
    auto edgeList = graph->ToEdgeListBfs();
    LOG_TRACE("edgeList size: {}", edgeList.size());
    const bool statisticsEnabled = pipelineContext != nullptr && pipelineContext->IsStatisticsEnabled();
    std::unordered_map<std::string, std::size_t> incomingEdgeCountByNode;
    std::unordered_map<std::string, std::size_t> outgoingEdgeCountByNode;

    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();
        if (!srcNode || !dstNode) {
            throw std::runtime_error("Expired node pointer in graph edge.");
        }
        if (incomingEdgeCountByNode.find(srcNode->name) == incomingEdgeCountByNode.end()) {
            incomingEdgeCountByNode.emplace(srcNode->name, 0);
        }
        if (outgoingEdgeCountByNode.find(dstNode->name) == outgoingEdgeCountByNode.end()) {
            outgoingEdgeCountByNode.emplace(dstNode->name, 0);
        }
        incomingEdgeCountByNode[dstNode->name] += 1;
        outgoingEdgeCountByNode[srcNode->name] += 1;
    }

    for (const auto& edge : edgeList) {
        auto srcNode = edge.srcNodePtr.lock();
        auto dstNode = edge.dstNodePtr.lock();

        if (!srcNode || !dstNode) {
            throw std::runtime_error("Expired node pointer in graph edge.");
        }

        const auto* existingSrcNodeIndex = plan.FindNodeIndex(srcNode->name);
        const auto srcNodeIndex = existingSrcNodeIndex != nullptr ? *existingSrcNodeIndex : [&]() {
            auto module = CreateModuleForGraphNode(srcNode);
            const bool isSourceNode = incomingEdgeCountByNode[srcNode->name] == 0;
            const bool isSinkNode = outgoingEdgeCountByNode[srcNode->name] == 0;
            auto runtimeProfile = BuildNodeRuntimeProfile(module, config, statisticsEnabled, isSourceNode, isSinkNode);
            return plan.EnsureNode(srcNode, module, std::move(runtimeProfile));
        }();
        const auto* existingDstNodeIndex = plan.FindNodeIndex(dstNode->name);
        const auto dstNodeIndex = existingDstNodeIndex != nullptr ? *existingDstNodeIndex : [&]() {
            auto module = CreateModuleForGraphNode(dstNode);
            const bool isSourceNode = incomingEdgeCountByNode[dstNode->name] == 0;
            const bool isSinkNode = outgoingEdgeCountByNode[dstNode->name] == 0;
            auto runtimeProfile = BuildNodeRuntimeProfile(module, config, statisticsEnabled, isSourceNode, isSinkNode);
            return plan.EnsureNode(dstNode, module, std::move(runtimeProfile));
        }();
        plan.AddQueueBinding(srcNodeIndex,
                             dstNodeIndex,
                             edge.srcPort,
                             edge.dstPort,
                             config.queueSize,
                             statisticsEnabled);
    }

    return plan;
}

/**
 * @brief 根据执行计划创建运行时对象。
 * @param plan 执行计划。
 * @return 物化结果。
 *
 * 这一步会：
 * - 创建 ModuleNode
 * - 创建队列
 * - 建立输入/输出绑定
 * - 将队列所有权保存到 Pipeline::Impl::queues
 */
ErrorCode Pipeline::Impl::MaterializeRuntime(const ExecutionPlan& plan) {
    moduleNodes.clear();
    moduleNodes.reserve(plan.moduleNodes.size());

    for (const auto& plannedNode : plan.moduleNodes) {
        moduleNodes.push_back(
            std::make_shared<ModuleNode>(plannedNode.module,
                                         plannedNode.runtimeProfile.runtimeConfig,
                                         plannedNode.runtimeProfile.isSinkNode,
                                         pipelineContext,
                                         executor));
    }

    for (const auto& queueBinding : plan.queueBindings) {
        if (queueBinding.srcNodeIndex >= moduleNodes.size() || queueBinding.dstNodeIndex >= moduleNodes.size()) {
            throw std::runtime_error("Execution plan contains an invalid node index in queue bindings.");
        }

        const auto& srcPlannedNode = plan.GetNode(queueBinding.srcNodeIndex);
        const auto& dstPlannedNode = plan.GetNode(queueBinding.dstNodeIndex);
        auto& srcModuleNode = moduleNodes[queueBinding.srcNodeIndex];
        auto& dstModuleNode = moduleNodes[queueBinding.dstNodeIndex];

        auto queue = std::make_unique<MessageQueue>(queueBinding.queueSize);
        auto queueView = makeViewPtr(queue.get());
        executor::Executor::PortStatsStatePtr portStats;
        if (queueBinding.statisticsEnabled) {
            portStats = std::make_shared<executor::Executor::PortStatsState>(
                srcPlannedNode.nodeName, queueBinding.srcPort, dstPlannedNode.nodeName, queueBinding.dstPort);
        }

        srcModuleNode->AddOutputQueue(queueBinding.srcPort, dstPlannedNode.nodeName, queueBinding.dstPort, queueView, portStats);
        dstModuleNode->AddInputQueue(queueBinding.dstPort, queueView, portStats);

        queues.push_back(std::move(queue));
    }

    return ErrorCode::SUCCESS;
}

/**
 * @brief 完成 Pipeline 初始化。
 * @return 初始化结果。
 */
ErrorCode Pipeline::Impl::Init() {
    LOG_TRACE("Try init pipeline with graph, [graphName={}]", graph->GetName());
    auto validationResult = ValidateGraph();
    if (validationResult != ErrorCode::SUCCESS) {
        return validationResult;
    }

    auto plan = BuildExecutionPlan();
    auto materializeResult = MaterializeRuntime(plan);
    if (materializeResult != ErrorCode::SUCCESS) {
        return materializeResult;
    }

    auto executorThreadCount = ResolveExecutorThreadCount(config.executorThreadCount, moduleNodes.size());
    pipelineContext->SetExecutorThreadCount(executorThreadCount);
    executor->SetThreadCount(executorThreadCount);

    ApplyTopologyPolicies();

    return ErrorCode::SUCCESS;
}

/**
 * @brief 执行拓扑层面的附加分析。
 *
 * 目前只保留 converge 节点探测，方便后续扩展 fork-join 优化策略。
 */
void Pipeline::Impl::ApplyTopologyPolicies() {
    if (!graph) return;

    auto convergeNodes = graph->GetConvergeNodes();
    LOG_DEBUG("Topology analysis: found {} converge nodes", convergeNodes.size());
    (void)convergeNodes;
}

/**
 * @brief 注册一个 Pipeline 观察者。
 * @param observer 目标观察者。
 */
void Pipeline::Impl::AddObserver(const std::shared_ptr<IPipelineObserver>& observer) {
    if (!observer) {
        return;
    }

    std::lock_guard<std::mutex> lock(observerMutex);
    if (std::find(observers.begin(), observers.end(), observer) == observers.end()) {
        observers.push_back(observer);
    }
}

/**
 * @brief 移除一个 Pipeline 观察者。
 * @param observer 目标观察者。
 */
void Pipeline::Impl::RemoveObserver(const std::shared_ptr<IPipelineObserver>& observer) {
    if (!observer) {
        return;
    }

    std::lock_guard<std::mutex> lock(observerMutex);
    observers.erase(std::remove(observers.begin(), observers.end(), observer), observers.end());
}

/**
 * @brief 通知观察者 Pipeline 已初始化。
 */
void Pipeline::Impl::NotifyPipelineInitialized() {
    const auto pipelineName = GetPipelineName();
    std::vector<std::shared_ptr<IPipelineObserver>> snapshot;
    {
        std::lock_guard<std::mutex> lock(observerMutex);
        snapshot = observers;
    }

    for (const auto& observer : snapshot) {
        if (!observer) {
            continue;
        }
        try {
            observer->OnPipelineInitialized(pipelineName);
        } catch (const std::exception& e) {
            LOG_WARN("Pipeline observer threw during OnPipelineInitialized: {}", e.what());
        } catch (...) {
            LOG_WARN("Pipeline observer threw during OnPipelineInitialized.");
        }
    }
}

/**
 * @brief 通知观察者 Pipeline 已启动。
 */
void Pipeline::Impl::NotifyPipelineStarted() {
    const auto pipelineName = GetPipelineName();
    std::vector<std::shared_ptr<IPipelineObserver>> snapshot;
    {
        std::lock_guard<std::mutex> lock(observerMutex);
        snapshot = observers;
    }

    for (const auto& observer : snapshot) {
        if (!observer) {
            continue;
        }
        try {
            observer->OnPipelineStarted(pipelineName);
        } catch (const std::exception& e) {
            LOG_WARN("Pipeline observer threw during OnPipelineStarted: {}", e.what());
        } catch (...) {
            LOG_WARN("Pipeline observer threw during OnPipelineStarted.");
        }
    }
}

/**
 * @brief 通知观察者 Pipeline 已停止。
 */
void Pipeline::Impl::NotifyPipelineStopped() {
    const auto pipelineName = GetPipelineName();
    std::vector<std::shared_ptr<IPipelineObserver>> snapshot;
    {
        std::lock_guard<std::mutex> lock(observerMutex);
        snapshot = observers;
    }

    for (const auto& observer : snapshot) {
        if (!observer) {
            continue;
        }
        try {
            observer->OnPipelineStopped(pipelineName);
        } catch (const std::exception& e) {
            LOG_WARN("Pipeline observer threw during OnPipelineStopped: {}", e.what());
        } catch (...) {
            LOG_WARN("Pipeline observer threw during OnPipelineStopped.");
        }
    }
}

/**
 * @brief 通知观察者 Pipeline 已完成反初始化。
 */
void Pipeline::Impl::NotifyPipelineDeInitialized() {
    const auto pipelineName = GetPipelineName();
    std::vector<std::shared_ptr<IPipelineObserver>> snapshot;
    {
        std::lock_guard<std::mutex> lock(observerMutex);
        snapshot = observers;
    }

    for (const auto& observer : snapshot) {
        if (!observer) {
            continue;
        }
        try {
            observer->OnPipelineDeInitialized(pipelineName);
        } catch (const std::exception& e) {
            LOG_WARN("Pipeline observer threw during OnPipelineDeInitialized: {}", e.what());
        } catch (...) {
            LOG_WARN("Pipeline observer threw during OnPipelineDeInitialized.");
        }
    }
}

/**
 * @brief 通知观察者发生生命周期错误。
 * @param code 错误码。
 * @param stage 发生错误的阶段。
 * @param nodeName 相关节点名称。
 * @param message 错误描述。
 */
void Pipeline::Impl::NotifyPipelineError(ErrorCode code,
                                         const std::string& stage,
                                         const std::string& nodeName,
                                         const std::string& message) {
    PipelineErrorEvent event;
    event.pipelineName = GetPipelineName();
    event.stage = stage;
    event.nodeName = nodeName;
    event.code = code;
    event.message = message;

    std::vector<std::shared_ptr<IPipelineObserver>> snapshot;
    {
        std::lock_guard<std::mutex> lock(observerMutex);
        snapshot = observers;
    }

    for (const auto& observer : snapshot) {
        if (!observer) {
            continue;
        }
        try {
            observer->OnPipelineError(event);
        } catch (const std::exception& e) {
            LOG_WARN("Pipeline observer threw during OnPipelineError: {}", e.what());
        } catch (...) {
            LOG_WARN("Pipeline observer threw during OnPipelineError.");
        }
    }
}

/**
 * @brief 通知观察者发生消息事件。
 * @param event 消息事件。
 */
void Pipeline::Impl::NotifyMessageEvent(const PipelineMessageEvent& event) {
    PipelineMessageEvent normalizedEvent = event;
    if (normalizedEvent.pipelineName.empty()) {
        normalizedEvent.pipelineName = GetPipelineName();
    }

    std::vector<std::shared_ptr<IPipelineObserver>> snapshot;
    {
        std::lock_guard<std::mutex> lock(observerMutex);
        snapshot = observers;
    }

    for (const auto& observer : snapshot) {
        if (!observer) {
            continue;
        }
        try {
            observer->OnMessageEvent(normalizedEvent);
        } catch (const std::exception& e) {
            LOG_WARN("Pipeline observer threw during OnMessageEvent: {}", e.what());
        } catch (...) {
            LOG_WARN("Pipeline observer threw during OnMessageEvent.");
        }
    }
}

} // namespace nexusflow
