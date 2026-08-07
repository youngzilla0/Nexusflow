#ifndef NEXUSFLOW_EXECUTOR_SCHEDULING_POLICY_HPP
#define NEXUSFLOW_EXECUTOR_SCHEDULING_POLICY_HPP

#include <nexusflow/Module.hpp>
#include <nexusflow/TopologyTypes.hpp>

#include <chrono>
#include <cstddef>
#include <memory>

namespace nexusflow { namespace executor {

/**
 * @brief Executor 在运行期使用的统一执行模式。
 *
 * 模块声明的 TriggerPolicy 会先被归一化为该枚举，
 * 之后 Executor 仅基于该枚举驱动执行主循环。
 */
enum class TaskExecutionMode {
    Source,      // 无输入源模块，直接调用 Process() 产出数据。
    OnAnyInput,  // 任一输入端口到达消息时执行一次。
    OnAllInputs, // 待全部输入端口就绪后执行一次。
};

/**
 * @brief 调度策略输入上下文。
 *
 * 该结构仅包含策略决策所需的静态属性，不暴露 Executor 的内部运行时状态。
 */
struct SchedulingContext {
    bool isSourceActor = false;
    Module::SourcePolicy sourcePolicy = Module::SourcePolicy::Polling;
    Module::TriggerPolicy triggerPolicy = Module::TriggerPolicy::Auto;
    std::size_t idleWaitUs = 0;
    const GraphTopologyInfo* topologyInfo = nullptr;
    bool isBranchActor = false;
    bool isJoinActor = false;
    bool isForkJoinActor = false;
    std::size_t localBranchFanOut = 0;
    std::size_t localJoinFanIn = 0;
    std::size_t branchCount = 0;
    std::size_t joinCount = 0;
    std::size_t forkJoinGroupCount = 0;
};

/**
 * @brief 单轮任务执行计划。
 */
struct TaskExecutionPlan {
    TaskExecutionMode executionMode = TaskExecutionMode::OnAnyInput;
    std::size_t maxStepsPerTask = 1; // 单次取得 worker 后允许连续执行的最大步数。
};

/**
 * @brief 单轮任务执行反馈。
 *
 * Executor 在一轮任务结束后构造该结构，并将其回传给调度策略。
 */
struct SchedulingFeedback {
    std::size_t completedSteps = 0; // 本轮实际完成的步数。
    bool madeProgress = false;      // 本轮是否至少推进过一次执行状态。
    bool emittedOutputs = false;    // 本轮是否向下游产生过输出。
    bool stoppedByNoWork = false;   // 本轮是否因无可执行工作而提前结束。
    bool hasPendingSignals = false; // 执行期间是否收到新的 ready 信号。
    bool hasPendingWork = false;    // 本轮结束时是否仍存在可继续处理的工作。
};

/**
 * @brief 调度策略接口。
 *
 * 该接口描述 Executor 在单轮任务执行前、中、后的全部调度决策点。
 */
class SchedulingPolicy {
public:
    virtual ~SchedulingPolicy() = default;

    /**
     * @brief 返回策略名称。
     * @return 当前策略的人类可读名称。
     */
    virtual const char* Name() const = 0;

    /**
     * @brief 为当前 actor 生成单轮执行计划。
     * @param context 当前 actor 的调度上下文。
     * @return 本轮任务应采用的执行模式与步数预算。
     */
    virtual TaskExecutionPlan Plan(const SchedulingContext& context) const = 0;

    /**
     * @brief 决定 Pipeline 启动时是否应主动提交该 actor。
     * @param context 当前 actor 的调度上下文。
     * @param hasPendingWork 启动时是否已检测到待处理工作。
     * @return 如果应立即提交到线程池，则返回 true。
     */
    virtual bool ShouldPrimeActorOnStart(const SchedulingContext& context, bool hasPendingWork) const = 0;

    /**
     * @brief 计算 source actor 空转时的退避时长。
     * @param context 当前 actor 的调度上下文。
     * @param feedback 本轮任务的执行反馈。
     * @return 下一轮再次调度前应等待的时长。
     */
    virtual std::chrono::microseconds IdleBackoff(const SchedulingContext& context,
                                                  const SchedulingFeedback& feedback) const = 0;

    /**
     * @brief 决定本轮任务结束后是否应立即重新入队。
     * @param context 当前 actor 的调度上下文。
     * @param feedback 本轮任务的执行反馈。
     * @return 如果应立即重新提交任务，则返回 true。
     */
    virtual bool ShouldReschedule(const SchedulingContext& context, const SchedulingFeedback& feedback) const = 0;
};

/**
 * @brief 根据线程数创建内部调度策略实例。
 * @param threadCount Executor 解析得到的线程数。
 * @return 对应线程配置下的调度策略对象。
 */
std::unique_ptr<SchedulingPolicy> CreateSchedulingPolicy(std::size_t threadCount);

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_SCHEDULING_POLICY_HPP
