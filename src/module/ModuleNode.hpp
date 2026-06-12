#ifndef NEXUSFLOW_MODULE_NODE_HPP
#define NEXUSFLOW_MODULE_NODE_HPP

#include "executor/Executor.hpp"
#include "nexusflow/ErrorCode.hpp"
#include <nexusflow/Module.hpp>

namespace nexusflow {

/**
 * @brief Module 的运行时包装层。
 *
 * ModuleNode 负责将一个业务 Module 接入 Pipeline 运行时：
 * - 为 Module 注入 Executor 与 PipelineContext
 * - 将 Module 注册到 Executor
 * - 作为 Pipeline 生命周期中的模块节点包装对象存在
 *
 * 当前代码中它更接近“运行时模块句柄”，而不是独立调度状态本体。
 * 真正由 Executor 调度的内部状态位于 NodeRegistry::NodeState。
 */
class ModuleNode {
public:
    /**
     * @brief 构造一个 ModuleNode。
     * @param module 业务模块实例。
     * @param runtimeConfig 该模块对应的运行时配置。
     * @param pipelineContext 所属 Pipeline 的共享上下文。
     * @param executor 所属 Pipeline 的共享 Executor。
     */
    ModuleNode(const std::shared_ptr<Module>& module,
               const PipelineConfig& runtimeConfig,
               const std::shared_ptr<PipelineContext>& pipelineContext,
               const std::shared_ptr<executor::Executor>& executor);

    /** @brief 析构 ModuleNode。 */
    ~ModuleNode();

    /**
     * @brief 为该模块绑定一个输入队列。
     * @param portName 输入端口名。
     * @param queue 关联消息队列。
     * @param stats 该边的统计状态。
     */
    void AddInputQueue(const std::string& portName, ViewPtr<MessageQueue> queue,
                       const executor::Executor::PortStatsStatePtr& stats) {
        m_executor->AddInputQueue(m_module->GetModuleName(), portName, queue, stats);
    }

    /**
     * @brief 为该模块绑定一个输出订阅边。
     * @param outputPortName 源输出端口名。
     * @param dstActorName 目标模块名。
     * @param dstInputPortName 目标输入端口名。
     * @param queue 关联消息队列。
     * @param stats 该边的统计状态。
     */
    void AddOutputQueue(const std::string& outputPortName, const std::string& dstActorName, const std::string& dstInputPortName,
                        ViewPtr<MessageQueue> queue, const executor::Executor::PortStatsStatePtr& stats) {
        m_executor->AddOutputQueue(m_module->GetModuleName(), outputPortName, dstActorName, dstInputPortName, queue, stats);
    }

    /** @brief 返回内部持有的 Module 实例。 */
    std::shared_ptr<Module>& GetModule() { return m_module; }

    /** @brief 返回模块名称。 */
    std::string GetModuleName() const { return m_module->GetModuleName(); }

    /** @brief 调用模块的 Init 生命周期。 */
    ErrorCode Init();

    /** @brief 调用模块的 DeInit 生命周期。 */
    ErrorCode DeInit();

    /** @brief 启动所属 Executor。 */
    ErrorCode Start();

    /** @brief 停止所属 Executor。 */
    ErrorCode Stop();

private:
    std::shared_ptr<Module> m_module;
    std::shared_ptr<executor::Executor> m_executor;
};
} // namespace nexusflow

#endif
