
#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "base/GraphUtils.hpp"
#include "utils/logging.hpp"
#include <nexusflow/ModuleFactory.hpp>

#include "impl/PipelineImpl.hpp"
#include <memory>
#include <nexusflow/Error.hpp>
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
    m_pImpl->executor->SetMessageEventCallback([this](const PipelineMessageEvent& event) {
        if (m_pImpl) {
            m_pImpl->NotifyMessageEvent(event);
        }
    });
    m_pImpl->graph = std::move(graph);
    m_pImpl->config = config;
    auto initResult = m_pImpl->Init();
    if (initResult.IsErr()) {
        throw std::runtime_error("Failed to initialize pipeline runtime from graph.");
    }
}

// --- Public APIs ---
std::unique_ptr<Pipeline> Pipeline::CreateFromYaml(const std::string& configPath) {
    auto pipeline = std::unique_ptr<Pipeline>(new Pipeline());
    pipeline->InitWithGraph(graphutils::CreateGraphFromYaml(configPath), graphutils::LoadPipelineConfigFromYaml(configPath));
    return pipeline;
}

Error Pipeline::Init() {
    if (!m_pImpl) return Error::Err(Error::Code::Uninitialized, "Pipeline is uninitialized.");

    for (auto& moduleNode : m_pImpl->moduleNodes) {
        Error errCode = moduleNode->Init();
        if (errCode.IsErr()) {
            LOG_ERROR("Init module failed, nodeName={}", moduleNode->GetModuleName());
            m_pImpl->NotifyPipelineError(errCode, "Init", moduleNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Init module success, nodeName={}", moduleNode->GetModuleName());
        }
    }
    m_pImpl->NotifyPipelineInitialized();
    return Error::Ok();
}

Error Pipeline::DeInit() {
    if (!m_pImpl) {
        return Error::Ok(); // Nothing to de-initialize
    }
    LOG_DEBUG("De-initializing pipeline...");

    // Reverse order
    for (auto it = m_pImpl->moduleNodes.rbegin(); it != m_pImpl->moduleNodes.rend(); ++it) {
        auto& moduleNode = *it;
        Error errCode = moduleNode->DeInit();
        if (errCode.IsErr()) {
            LOG_ERROR("DeInit module failed, nodeName={}", moduleNode->GetModuleName());
            m_pImpl->NotifyPipelineError(errCode, "DeInit", moduleNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("DeInit module success, nodeName={}", moduleNode->GetModuleName());
        }
    }

    LOG_DEBUG("Pipeline de-initialized successfully.");
    m_pImpl->NotifyPipelineDeInitialized();
    return Error::Ok();
}

Error Pipeline::Start() {
    if (!m_pImpl) {
        LOG_ERROR("Cannot start pipeline: not initialized.");
        return Error::Err(Error::Code::Uninitialized, "Pipeline is uninitialized.");
    }
    LOG_DEBUG("Starting pipeline...");

    for (auto& moduleNode : m_pImpl->moduleNodes) {
        Error errCode = moduleNode->Start();
        if (errCode.IsErr()) {
            LOG_ERROR("Start worker failed, nodeName={}", moduleNode->GetModuleName());
            m_pImpl->NotifyPipelineError(errCode, "Start", moduleNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Start module success, nodeName={}", moduleNode->GetModuleName());
        }
    }
    LOG_DEBUG("Pipeline started successfully.");
    m_pImpl->NotifyPipelineStarted();
    return Error::Ok();
}

Error Pipeline::Stop() {
    if (!m_pImpl) {
        return Error::Ok(); // Nothing to stop.
    }

    LOG_DEBUG("Stopping pipeline...");
    // TODO: 优化一下.
    for (auto& queue : m_pImpl->queues) {
        queue->Shutdown();
    }
    Error errCode = Error::Ok();
    for (auto& moduleNode : m_pImpl->moduleNodes) {
        errCode = moduleNode->Stop();
        if (errCode.IsErr()) {
            LOG_ERROR("Stop worker failed, nodeName={}", moduleNode->GetModuleName());
            m_pImpl->NotifyPipelineError(errCode, "Stop", moduleNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Stop module success, nodeName={}", moduleNode->GetModuleName());
        }
    }
    LOG_DEBUG("Pipeline stopped successfully.");
    m_pImpl->NotifyPipelineStopped();
    return Error::Ok();
}

void Pipeline::AddObserver(const std::shared_ptr<IPipelineObserver>& observer) {
    if (!m_pImpl) {
        return;
    }
    m_pImpl->AddObserver(observer);
}

void Pipeline::RemoveObserver(const std::shared_ptr<IPipelineObserver>& observer) {
    if (!m_pImpl) {
        return;
    }
    m_pImpl->RemoveObserver(observer);
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

PipelineSummaryStats Pipeline::GetSummaryStats() const {
    if (!m_pImpl || !m_pImpl->executor) {
        return {};
    }
    return m_pImpl->executor->GetSummaryStats();
}

}; // namespace nexusflow
