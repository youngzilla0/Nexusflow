#ifndef NEXUSFLOW_EXECUTOR_HPP
#define NEXUSFLOW_EXECUTOR_HPP

#include "JoinStateStore.hpp"
#include "Statistics.hpp"
#include "SchedulingPolicy.hpp"
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
 * Executor 负责 actor 注册、队列绑定、任务调度、消息分发、
 * join 状态协调以及运行时统计汇总入口。
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
     */
    void RegisterNode(const std::string& nodeName,
                       const std::shared_ptr<Module>& module,
                       const PipelineConfig& runtimeConfig);

    /**
     * @brief 为 actor 绑定一个输入队列。
     * @param nodeName 目标 actor 名称。
     * @param inputPortName 输入端口名。
     * @param queue 与该输入端口关联的消息队列。
     * @param stats 该边对应的统计状态。
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
     */
    void AddOutputQueue(const std::string& nodeName, const std::string& outputPortName, const std::string& dstActorName,
                        const std::string& dstInputPortName, ViewPtr<MessageQueue> queue,
                        const PortStatsStatePtr& stats);

    /**
     * @brief 设置 Executor 的线程数配置。
     * @param threadCount 期望线程数，0 表示运行期自动推导。
     */
    void SetThreadCount(std::size_t threadCount);

    /**
     * @brief 向 actor 的全部广播订阅者分发一条消息。
     * @param nodeName 源 actor 名称。
     * @param message 待分发消息。
     * @param blocking 是否采用阻塞推送。
     */
    void Emit(const std::string& nodeName, const Message& message, bool blocking);

    /**
     * @brief 向 actor 的指定输出端口订阅者分发一条消息。
     * @param nodeName 源 actor 名称。
     * @param outputPortName 源输出端口名称。
     * @param message 待分发消息。
     * @param blocking 是否采用阻塞推送。
     */
    void Route(const std::string& nodeName, const std::string& outputPortName, const Message& message, bool blocking);

    /**
     * @brief 返回当前全部边级统计快照。
     * @return 边级统计快照列表。
     */
    std::vector<PortStats> GetPortStats() const;

    /**
     * @brief 返回当前全部节点级统计快照。
     * @return 节点级统计快照列表。
     */
    std::vector<NodeStats> GetNodeStats() const;

    /**
     * @brief 启动线程池并开始调度可运行 actor。
     */
    void Start();

    /**
     * @brief 停止调度并关闭线程池。
     */
    void Stop();

private:
    /**
     * @brief actor 输入端口与底层队列的绑定关系。
     */
    struct InputQueueBinding {
        std::string inputPortName;
        ViewPtr<MessageQueue> queue;
        PortStatsStatePtr stats;
    };

    /**
     * @brief 单个 actor 在 Executor 调度层中的运行时状态。
     *
     * 该结构是 Executor 的私有内部状态，不直接暴露给 Pipeline 或 Module。
     * 为避免与 ModuleNode 的包装层职责混淆，此处显式命名为 ScheduledActorState。
     */
    struct ScheduledActorState {
        std::string nodeName;
        std::shared_ptr<Module> module;
        PipelineConfig runtimeConfig;
        std::vector<InputQueueBinding> inputQueues;
        std::size_t nextInputIndex = 0; // OnAnyInput 下用于轮转扫描输入队列，避免总是偏向第一个端口。
        JoinStateStore joinState;       // 只在 OnAllInputs 下使用，缓存待拼齐的 join group。
        std::atomic<bool> taskScheduled{false}; // 当前 actor 是否已经在池中排队或执行，避免重复提交。
        std::atomic<std::uint64_t> pendingRunSignals{0}; // 记录执行期间新增的“需要再跑一次”信号。
        NodeStatsStatePtr stats = std::make_shared<NodeStatsState>();
    };

    /**
     * @brief 单个输出订阅边的运行时绑定信息。
     */
    struct OutputSubscriber {
        std::string dstActorName;
        std::string dstInputPortName;
        ViewPtr<MessageQueue> queue;
        PortStatsStatePtr stats;
        std::shared_ptr<ScheduledActorState> dstActorState;
    };

    /**
     * @brief 单个 step 的执行结果。
     *
     * StepResult 描述单步执行的局部结果；
     * SchedulingFeedback 描述整轮任务的累计结果。
     */
    struct StepResult {
        bool madeProgress = false;   // 当前 step 是否实际推进了执行状态。
        bool emittedOutputs = false; // 当前 step 是否产生了下游输出。
    };

private:
    /**
     * @brief 从 ScheduledActorState 中提取调度策略所需信息。
     * @param state 当前 actor 的运行时状态。
     * @return 可供调度策略使用的上下文。
     */
    SchedulingContext BuildSchedulingContext(const ScheduledActorState& state) const;

    /**
     * @brief 根据当前 joinKeyPolicy 解析消息的 join key。
     * @param state 当前 actor 的运行时状态。
     * @param message 待解析消息。
     * @return 用于 OnAllInputs 拼接的 join key。
     */
    std::uint64_t ResolveJoinKey(const ScheduledActorState& state, const Message& message) const;

    /**
     * @brief 判断运行时统计是否启用。
     * @return 启用统计时返回 true。
     */
    bool StatisticsEnabled() const;

    /**
     * @brief 在 Pipeline 启动时提交所有应立即执行的 actor。
     */
    void PrimeActorsOnStart();

    /**
     * @brief 将 actor 提交到线程池执行。
     * @param state 目标 actor 的运行时状态。
     */
    void SubmitActorTask(const std::shared_ptr<ScheduledActorState>& state);

    /**
     * @brief 标记 actor 为 ready，并在必要时触发任务提交。
     * @param state 目标 actor 的运行时状态。
     */
    void NotifyActorReady(const std::shared_ptr<ScheduledActorState>& state);

    /**
     * @brief 判断 actor 当前是否仍存在可继续处理的工作。
     * @param state 目标 actor 的运行时状态。
     * @return 若仍有工作可做，则返回 true。
     */
    bool HasPendingWork(const std::shared_ptr<ScheduledActorState>& state) const;

    /**
     * @brief actor 在线程池中的执行入口。
     * @param state 目标 actor 的运行时状态。
     *
     * 该函数负责生成执行计划、循环执行 step，并在结束后决定是否重新调度。
     */
    void RunActorTask(const std::shared_ptr<ScheduledActorState>& state);

    /**
     * @brief 执行 source actor 的单个 step。
     * @param state 目标 actor 的运行时状态。
     * @return 当前 step 的执行结果。
     */
    StepResult RunSourceStep(const std::shared_ptr<ScheduledActorState>& state);

    /**
     * @brief 执行 OnAnyInput actor 的单个 step。
     * @param state 目标 actor 的运行时状态。
     * @return 当前 step 的执行结果。
     */
    StepResult RunOnAnyInputStep(const std::shared_ptr<ScheduledActorState>& state);

    /**
     * @brief 执行 OnAllInputs actor 的单个 step。
     * @param state 目标 actor 的运行时状态。
     * @return 当前 step 的执行结果。
     */
    StepResult RunOnAllInputsStep(const std::shared_ptr<ScheduledActorState>& state);

    /**
     * @brief 从任一输入端口尝试提取一条消息。
     * @param state 目标 actor 的运行时状态。
     * @param portMessage 输出参数，用于接收提取到的端口消息。
     * @return 成功提取消息时返回 true。
     */
    bool TryPopAnyInput(const std::shared_ptr<ScheduledActorState>& state, PortMessage& portMessage);

    /**
     * @brief 分发模块本轮产生的全部输出。
     * @param nodeName 源 actor 名称。
     * @param outputs 模块本轮产生的输出集合。
     */
    void DispatchOutputs(const std::string& nodeName, PortOutputs& outputs);

    /**
     * @brief 将一条消息分发给单个下游订阅者。
     * @param subscriber 目标订阅边。
     * @param message 待分发消息。
     * @param blocking 是否采用阻塞推送。
     */
    void DispatchToSubscriber(const OutputSubscriber& subscriber, const Message& message, bool blocking);

private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, std::shared_ptr<ScheduledActorState>> m_actorStates;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_broadcastSubscribers;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_outputSubscribers;
    Statistics m_statsCollector;
    std::shared_ptr<PipelineContext> m_pipelineContext;
    std::unique_ptr<ThreadPool> m_threadPool;
    std::unique_ptr<SchedulingPolicy> m_schedulingPolicy;
    std::size_t m_threadCount = 0;
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_stopFlag{false};
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_HPP
