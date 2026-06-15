#include "ModuleNode.hpp"
#include "nexusflow/ErrorCode.hpp"

#include <memory>

namespace nexusflow {

/**
 * @brief 构造一个 ModuleNode。
 * @param module 业务模块实例。
 * @param runtimeConfig 该模块对应的运行时配置。
 * @param isSinkNode 该模块在当前拓扑中是否为 sink 节点。
 * @param pipelineContext 所属 Pipeline 的共享上下文。
 * @param executor 所属 Pipeline 的共享 Executor。
 */
ModuleNode::ModuleNode(const std::shared_ptr<Module>& module,
                       const PipelineConfig& runtimeConfig,
                       bool isSinkNode,
                       const std::shared_ptr<PipelineContext>& pipelineContext,
                       const std::shared_ptr<executor::Executor>& executor) {
    m_module = module;
    m_executor = executor;
    m_module->SetExecutor(m_executor);
    m_module->SetPipelineContext(pipelineContext);
    m_executor->RegisterNode(m_module->GetModuleName(), m_module, runtimeConfig, isSinkNode);
}

/** @brief 析构 ModuleNode。 */
ModuleNode::~ModuleNode() = default;

/** @brief 调用模块的 Init 生命周期。 */
ErrorCode ModuleNode::Init() { return m_module->Init(); }

/** @brief 调用模块的 DeInit 生命周期。 */
ErrorCode ModuleNode::DeInit() { return m_module->DeInit(); }

/** @brief 启动所属 Executor。 */
ErrorCode ModuleNode::Start() {
    m_executor->Start();
    return ErrorCode::SUCCESS;
}

/** @brief 停止所属 Executor。 */
ErrorCode ModuleNode::Stop() {
    m_executor->Stop();
    return ErrorCode::SUCCESS;
}

} // namespace nexusflow
