#ifndef NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
#define NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP

#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "executor/Executor.hpp"
#include "module/ModuleNode.hpp"
#include "pipeline/ExecutionPlan.hpp"
#include <nexusflow/PipelineEvents.hpp>
#include <nexusflow/PipelineContext.hpp>
#include <nexusflow/Pipeline.hpp>
#include <mutex>
#include <string>
#include <vector>

namespace nexusflow {

// Forward declarations
class Pipeline;

/**
 * @brief Pipeline 的私有实现。
 *
 * 这个类承接 Pipeline 的所有运行时组装工作：
 * - 持有图、队列、上下文和 Executor
 * - 根据 Graph 生成 ExecutionPlan
 * - 把 ExecutionPlan 物化成可运行的 ModuleNode / MessageQueue
 * - 转发 Pipeline 事件给观察者
 */
class Pipeline::Impl {
public:
    std::unique_ptr<Graph> graph; ///< 当前 Pipeline 的图结构。
    std::vector<MessageQueueUPtr> queues; ///< 运行时创建并持有的全部消息队列。
    PipelineConfig config; ///< Pipeline 的运行时配置快照。
    std::shared_ptr<PipelineContext> pipelineContext; ///< 所有模块共享的上下文。
    std::shared_ptr<executor::Executor> executor; ///< 运行时调度与路由核心。

    std::vector<std::shared_ptr<ModuleNode>> moduleNodes; ///< 物化后的模块节点列表，按拓扑顺序保存。
    GraphTopologyInfo topologyInfo; ///< 由 Graph 派生出的拓扑分析结果。

    /**
     * @brief 执行 Pipeline 初始化。
     * @return 初始化结果码。
     *
     * 该函数会依次执行图校验、执行计划构建和运行时物化。
     */
    Error Init();

    /**
     * @brief 执行拓扑相关的额外策略分析。
     *
     * 目前主要用于对 converge / fork-join 这类结构做预分析。
     */
    void ApplyTopologyPolicies();

    /**
     * @brief 注册一个 Pipeline 观察者。
     * @param observer 目标观察者。
     */
    void AddObserver(const std::shared_ptr<IPipelineObserver>& observer);

    /**
     * @brief 移除一个 Pipeline 观察者。
     * @param observer 目标观察者。
     */
    void RemoveObserver(const std::shared_ptr<IPipelineObserver>& observer);

    /**
     * @brief 通知观察者 Pipeline 已初始化。
     */
    void NotifyPipelineInitialized();

    /**
     * @brief 通知观察者 Pipeline 已启动。
     */
    void NotifyPipelineStarted();

    /**
     * @brief 通知观察者 Pipeline 已停止。
     */
    void NotifyPipelineStopped();

    /**
     * @brief 通知观察者 Pipeline 已完成反初始化。
     */
    void NotifyPipelineDeInitialized();

    /**
     * @brief 通知观察者发生了 Pipeline 生命周期错误。
     * @param code 错误码。
     * @param stage 发生错误的阶段。
     * @param nodeName 相关节点名称。
     * @param message 错误描述。
     */
    void NotifyPipelineError(const Error& error, const std::string& stage, const std::string& nodeName);

    /**
     * @brief 通知观察者发生消息投递事件。
     * @param event 消息事件。
     */
    void NotifyMessageEvent(const PipelineMessageEvent& event);

    /**
     * @brief 获取当前 Pipeline 的名称。
     * @return Pipeline 名称。
     */
    std::string GetPipelineName() const;

    /**
     * @brief 获取当前 Pipeline 的拓扑分析结果。
     * @return 拓扑分析结果快照。
     */
    const GraphTopologyInfo& GetTopologyInfo() const;

private:
    /**
     * @brief 校验图结构是否可以进入运行时物化。
     * @return 校验结果。
     */
    Error ValidateGraph() const;

    /**
     * @brief 将 Graph 转换为稳定的执行计划。
     * @return 生成的执行计划。
     */
    ExecutionPlan BuildExecutionPlan() const;

    /**
     * @brief 根据执行计划创建运行时对象。
     * @param plan 执行计划。
     * @return 物化结果。
     */
    Error MaterializeRuntime(const ExecutionPlan& plan);

private:
    std::vector<std::shared_ptr<IPipelineObserver>> observers; ///< 已注册的观察者列表。
    mutable std::mutex observerMutex; ///< 保护观察者列表的互斥锁。
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
