
#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "base/GraphUtils.hpp"
#include "utils/logging.hpp"
#include <nexusflow/ModuleFactory.hpp>

#include "impl/PipelineImpl.hpp"
#include <memory>
#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/Module.hpp>
#include <nexusflow/Pipeline.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace nexusflow {

Pipeline::Pipeline() : m_pImpl(std::make_unique<Pipeline::Impl>()) {}

Pipeline::~Pipeline() = default;

void Pipeline::InitWithGraph(std::unique_ptr<Graph> graph, const PipelineConfig& config) {
    if (!graph) {
        throw std::invalid_argument("Pipeline graph cannot be null.");
    }

    LOG_DEBUG("Initializing pipeline with graph, graph={}", graph->ToString());
    m_pImpl->pipelineContext = std::shared_ptr<PipelineContext>(new PipelineContext(graph->GetName(), config));
    m_pImpl->executor = std::make_shared<executor::Executor>(m_pImpl->pipelineContext);
    m_pImpl->graph = std::move(graph);
    m_pImpl->config = config;
    auto initResult = m_pImpl->Init();
    if (initResult != ErrorCode::SUCCESS) {
        throw std::runtime_error("Failed to initialize pipeline runtime from graph.");
    }
}

// --- Public APIs ---
std::unique_ptr<Pipeline> Pipeline::CreateFromYaml(const std::string& configPath) {
    auto pipeline = std::unique_ptr<Pipeline>(new Pipeline());
    pipeline->InitWithGraph(graphutils::CreateGraphFromYaml(configPath), graphutils::LoadPipelineConfigFromYaml(configPath));
    return pipeline;
}

ErrorCode Pipeline::Init() {
    if (!m_pImpl) return ErrorCode::UNINITIALIZED_ERROR;

    for (auto& actorNode : m_pImpl->actorOrderedNodes) {
        ErrorCode errCode = actorNode->Init();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Init module failed, nodeName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Init module success, nodeName={}", actorNode->GetModuleName());
        }
    }
    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::DeInit() {
    if (!m_pImpl) {
        return ErrorCode::SUCCESS; // Nothing to de-initialize
    }
    LOG_DEBUG("De-initializing pipeline...");

    // Reverse order
    for (auto it = m_pImpl->actorOrderedNodes.rbegin(); it != m_pImpl->actorOrderedNodes.rend(); ++it) {
        auto& actorNode = *it;
        ErrorCode errCode = actorNode->DeInit();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("DeInit module failed, nodeName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("DeInit module success, nodeName={}", actorNode->GetModuleName());
        }
    }

    LOG_DEBUG("Pipeline de-initialized successfully.");
    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::Start() {
    if (!m_pImpl) {
        LOG_ERROR("Cannot start pipeline: not initialized.");
        return ErrorCode::UNINITIALIZED_ERROR;
    }
    LOG_DEBUG("Starting pipeline...");

    for (auto& actorNode : m_pImpl->actorOrderedNodes) {
        ErrorCode errCode = actorNode->Start();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Start worker failed, nodeName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Start module success, nodeName={}", actorNode->GetModuleName());
        }
    }
    LOG_DEBUG("Pipeline started successfully.");
    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::Stop() {
    if (!m_pImpl) {
        return ErrorCode::SUCCESS; // Nothing to stop.
    }

    LOG_DEBUG("Stopping pipeline...");
    // TODO: 优化一下.
    for (auto& queue : m_pImpl->queues) {
        queue->Shutdown();
    }
    ErrorCode errCode = ErrorCode::SUCCESS;
    for (auto& actorNode : m_pImpl->actorOrderedNodes) {
        errCode = actorNode->Stop();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Stop worker failed, nodeName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Stop module success, nodeName={}", actorNode->GetModuleName());
        }
    }
    LOG_DEBUG("Pipeline stopped successfully.");
    return ErrorCode::SUCCESS;
}

std::vector<PortStats> Pipeline::GetPortStats() const {
    if (!m_pImpl || !m_pImpl->executor) {
        return {};
    }
    return m_pImpl->executor->GetPortStats();
}

std::vector<NodeStats> Pipeline::GetNodeStats() const {
    if (!m_pImpl || !m_pImpl->executor) {
        return {};
    }
    return m_pImpl->executor->GetNodeStats();
}

}; // namespace nexusflow
