#ifndef NEXUSFLOW_EXECUTOR_HPP
#define NEXUSFLOW_EXECUTOR_HPP

#include "JoinStateStore.hpp"
#include "statistics/NodeStateRegistry.hpp"
#include "PortRouter.hpp"
#include "SchedulingPolicy.hpp"
#include "statistics/Statistics.hpp"
#include "ThreadPool.hpp"
#include "base/Define.hpp"
#include "common/ViewPtr.hpp"

#include <nexusflow/Module.hpp>
#include <nexusflow/PipelineConfig.hpp>
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/StatisticsTypes.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

/**
 * @brief Pipeline 运行期的核心执行协调器。
 *
 * Executor 负责节点任务调度、消息分发、
 * join 状态协调以及运行时统计汇总入口。
 *
 * 它是 Pipeline 在运行期的“调度中枢”：
 * - 接收 ModuleNode 注册进来的节点与队列绑定
 * - 根据调度策略把节点提交到线程池
 * - 从输入队列取消息并驱动 Module::Process
 * - 将输出结果路由到下游
 * - 同步更新统计与消息事件
 */
class Executor {
public:
    using PortStatsState = Statistics::PortStatsState;
    using PortStatsStatePtr = Statistics::PortStatsStatePtr;
    using NodeStatsState = Statistics::NodeStatsState;
    using NodeStatsStatePtr = Statistics::NodeStatsStatePtr;

    /**
     * @brief 构造一个 Executor。
     * @param pipelineContext 所属 Pipeline 的共享上下文。
     */
    explicit Executor(const std::shared_ptr<PipelineContext>& pipelineContext);

    /**
     * @brief 析构 Executor，并确保运行期资源被停止释放。
     */
    ~Executor();

    /**
     * @brief 注册一个 actor 及其运行时配置。
     * @param nodeName actor 名称。
     * @param module actor 持有的模块实例。
     * @param runtimeConfig actor 对应的运行时配置快照。
     * @param isSinkNode 当前节点是否为 sink 节点。
     */
    void RegisterNode(const std::string& nodeName,
                      const std::shared_ptr<Module>& module,
                      const PipelineConfig& runtimeConfig,
                      bool isSinkNode);

    /**
     * @brief 为 actor 绑定一个输入队列。
     * @param nodeName 目标 actor 名称。
     * @param inputPortName 输入端口名。
     * @param queue 与该输入端口关联的消息队列。
     * @param stats 该边对应的统计状态。
     *
     * 输入队列绑定完成后，Executor 才能在调度节点时从这些队列中拉取消息。
     */
    void AddInputQueue(const std::string& nodeName, const std::string& inputPortName, ViewPtr<MessageQueue> queue,
                       const PortStatsStatePtr& stats);

    /**
     * @brief 为 actor 绑定一个输出订阅边。
     * @param nodeName 源 actor 名称。
     * @param outputPortName 源输出端口名称。
     * @param dstActorName 目标 actor 名称。
     * @param dstInputPortName 目标输入端口名称。
     * @param queue 源到目标之间的消息队列。
     * @param stats 该边对应的统计状态。
     *
     * 这一步会把输出端口与所有订阅者的队列关系登记到 PortRouter，
     * 之后 Module 发出的 Broadcast / Route 都会经由这里投递。
     */
    void AddOutputQueue(const std::string& nodeName, const std::string& outputPortName, const std::string& dstActorName,
                        const std::string& dstInputPortName, ViewPtr<MessageQueue> queue,
                        const PortStatsStatePtr& stats);

    /**
     * @brief 设置 Executor 的线程数配置。
     * @param threadCount 期望线程数，0 表示运行期自动推导。
     *
     * 线程数最终会在 Start() 时解析成线程池大小。
     */
    void SetThreadCount(std::size_t threadCount);

    /**
     * @brief 设置消息投递异常事件回调。
     * @param callback 用于接收丢弃/拒绝事件的回调。
     *
     * 该回调只处理“投递层面的异常事件”，不包含统计快照。
     */
    void SetMessageEventCallback(PortRouter::MessageEventCallback callback);

    /**
     * @brief 向 actor 的全部广播订阅者分发一条消息。
     * @param nodeName 源 actor 名称。
     * @param message 待分发消息。
     * @param blocking 是否采用阻塞推送。
     *
     * 该函数会把消息交给 PortRouter，由后者负责广播到全部下游。
     */
    void Emit(const std::string& nodeName, const Message& message, bool blocking);

    /**
     * @brief 向 actor 的指定输出端口订阅者分发一条消息。
     * @param nodeName 源 actor 名称。
     * @param outputPortName 源输出端口名称。
     * @param message 待分发消息。
     * @param blocking 是否采用阻塞推送。
     *
     * 该函数用于路由到命名输出端口对应的订阅者集合。
     */
    void Route(const std::string& nodeName, const std::string& outputPortName, const Message& message, bool blocking);

    /**
     * @brief 返回当前全部边级统计快照。
     * @return 边级统计快照列表。
     *
     * 返回的是“边视角”的数据，用于观察队列吞吐、丢弃、拒绝和深度。
     */
    std::vector<PortStats> GetPortStats() const;

    /**
     * @brief 返回当前全部节点级统计快照。
     * @return 节点级统计快照列表。
     *
     * 返回的是“节点视角”的数据，用于观察模块执行次数、输入消费与 join 行为。
     */
    std::vector<NodeStats> GetNodeStats() const;

    /**
     * @brief 返回当前 Pipeline 级别的汇总统计。
     * @return 汇总后的吞吐、丢弃与端到端时延统计。
     *
     * 这部分数据用于生成面向报告和监控的 Pipeline 总览。
     */
    PipelineSummaryStats GetSummaryStats() const;

    /**
     * @brief 启动线程池并开始调度可运行 actor。
     *
     * 启动后 Executor 才会开始按照调度策略提交节点任务。
     */
    void Start();

    /**
     * @brief 停止调度并关闭线程池。
     *
     * 停止后不会再提交新任务，但已在执行中的任务会自然收尾。
     */
    void Stop();

private:
    /**
     * @brief 单个 step 的执行结果。
     *
     * StepResult 描述单步执行的局部结果；
     * SchedulingFeedback 描述整轮任务的累计结果。
     */
    struct StepResult {
        bool madeProgress = false;   ///< 当前 step 是否实际推进了执行状态。
        bool emittedOutputs = false; ///< 当前 step 是否产生了下游输出。
    };

private:
    /**
     * @brief 从 NodeStateRegistry::NodeState 中提取调度策略所需信息。
     * @param state 当前节点的运行时状态。
     * @return 可供调度策略使用的上下文。
     *
     * 这里会把节点的 source/trigger 策略、idleWaitUs 等信息整理成调度上下文。
     */
    SchedulingContext BuildSchedulingContext(const NodeStateRegistry::NodeState& state) const;

    /**
     * @brief 根据当前 joinKeyPolicy 解析消息的 join key。
     * @param state 当前节点的运行时状态。
     * @param message 待解析消息。
     * @return 用于 OnAllInputs 拼接的 join key。
     *
     * 该 key 决定同一轮 join 中哪些消息会被拼到同一组。
     */
    std::uint64_t ResolveJoinKey(const NodeStateRegistry::NodeState& state, const Message& message) const;

    /**
     * @brief 判断运行时统计是否启用。
     * @return 启用统计时返回 true。
     *
     * 这是 Executor 内部的轻量分支判断，避免在热路径上反复读取外部对象。
     */
    bool StatisticsEnabled() const;

    /**
     * @brief 在 Pipeline 启动时提交所有应立即执行的节点。
     *
     * 这一轮只负责初次唤醒，不会改变节点内部语义。
     */
    void PrimeNodesOnStart();

    /**
     * @brief 将节点提交到线程池执行。
     * @param state 目标节点的运行时状态。
     *
     * 该函数会先做 taskScheduled 原子保护，避免同一节点被重复提交。
     */
    void SubmitNodeTask(const NodeStateRegistry::NodeStatePtr& state);

    /**
     * @brief 标记节点为 ready，并在必要时触发任务提交。
     * @param state 目标节点的运行时状态。
     *
     * 当队列有新消息到达、或者节点自身需要补跑时，会通过这里触发。
     */
    void NotifyNodeReady(const NodeStateRegistry::NodeStatePtr& state);

    /**
     * @brief 判断节点当前是否仍存在可继续处理的工作。
     * @param state 目标节点的运行时状态。
     * @return 若仍有工作可做，则返回 true。
     *
     * 该判断用于决定节点任务结束后是否需要继续补调度。
     */
    bool HasPendingWork(const NodeStateRegistry::NodeStatePtr& state) const;

    /**
     * @brief 节点在线程池中的执行入口。
     * @param state 目标节点的运行时状态。
     *
     * 该函数负责生成执行计划、循环执行 step，并在结束后决定是否重新调度。
     *
     * 可以把它看成节点的一次“调度回合”。
     */
    void RunNodeTask(const NodeStateRegistry::NodeStatePtr& state);

    /**
     * @brief 执行 source 节点的单个 step。
     * @param state 目标节点的运行时状态。
     * @return 当前 step 的执行结果。
     *
     * source 节点没有输入，只负责主动产生输出。
     */
    StepResult RunSourceStep(const NodeStateRegistry::NodeStatePtr& state);

    /**
     * @brief 执行 OnAnyInput 节点的单个 step。
     * @param state 目标节点的运行时状态。
     * @return 当前 step 的执行结果。
     *
     * 该模式会从任一可用输入端口取一条消息后立即执行模块。
     */
    StepResult RunOnAnyInputStep(const NodeStateRegistry::NodeStatePtr& state);

    /**
     * @brief 执行 OnAllInputs 节点的单个 step。
     * @param state 目标节点的运行时状态。
     * @return 当前 step 的执行结果。
     *
     * 该模式会先收集所有输入端口的消息，再在 joinState 中拼出完整输入组。
     */
    StepResult RunOnAllInputsStep(const NodeStateRegistry::NodeStatePtr& state);

    /**
     * @brief 为 sink 节点记录单条消息的端到端时延。
     * @param state 目标节点状态。
     * @param message 当前输入消息。
     *
     * 仅在目标节点被标记为 sink 时生效。
     */
    void RecordSinkLatencyIfNeeded(const NodeStateRegistry::NodeStatePtr& state, const Message& message);

    /**
     * @brief 为 sink join 节点记录一组输入消息的端到端时延。
     * @param state 目标节点状态。
     * @param inputs 当前拼齐的一组输入消息。
     *
     * 对 join 场景使用该组输入中最大的消息年龄，
     * 以反映该轮聚合完成时的端到端等待上界。
     *
     * 仅在目标节点被标记为 sink 时生效。
     */
    void RecordSinkLatencyIfNeeded(const NodeStateRegistry::NodeStatePtr& state, const std::vector<PortMessage>& inputs);

    /**
     * @brief 从任一输入端口尝试提取一条消息。
     * @param state 目标节点的运行时状态。
     * @param portMessage 输出参数，用于接收提取到的端口消息。
     * @return 成功提取消息时返回 true。
     *
     * OnAnyInput 模式会通过轮转方式扫描输入队列，尽量避免固定偏向某个端口。
     */
    bool TryPopAnyInput(const NodeStateRegistry::NodeStatePtr& state, PortMessage& portMessage);

    /**
     * @brief 分发模块本轮产生的全部输出。
     * @param nodeName 源 actor 名称。
     * @param outputs 模块本轮产生的输出集合。
     *
     * 这里会先更新节点级输出统计，再把广播和定向路由分别交给 PortRouter。
     */
    void DispatchOutputs(const std::string& nodeName, PortOutputs& outputs);

private:
    NodeStateRegistry m_nodeRegistry; ///< 节点状态注册表，保存所有执行期节点状态。
    Statistics m_statsCollector; ///< 运行时统计聚合器。
    PortRouter m_portRouter; ///< 输出路由与消息投递组件。
    std::shared_ptr<PipelineContext> m_pipelineContext; ///< Pipeline 共享上下文。
    std::unique_ptr<ThreadPool> m_threadPool; ///< 节点执行使用的线程池。
    std::unique_ptr<SchedulingPolicy> m_schedulingPolicy; ///< 当前启用的调度策略对象。
    std::size_t m_threadCount = 0; ///< 外部配置或推导得到的线程数。
    std::atomic<bool> m_started{false}; ///< Executor 是否已经启动。
    std::atomic<bool> m_stopFlag{false}; ///< 运行期停止标记，用于让任务尽快退出。
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_HPP
