
#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "base/GraphUtils.hpp"
#include "common/ViewPtr.hpp"
#include "core/Worker.hpp"
#include "dispatcher/Dispatcher.hpp"
#include "utils/logging.hpp"
#include <nexusflow/ModuleFactory.hpp>

#include "impl/PipelineImpl.hpp"
#include <memory>
#include <nexusflow/ErrorCode.hpp>
#include <nexusflow/Module.hpp>
#include <nexusflow/Pipeline.hpp>
#include <string>
#include <unordered_map>

namespace nexusflow {

// --- Static methods ---
std::unique_ptr<Pipeline> Pipeline::CreateFromYaml(const std::string& configPath) {
    auto pipeline = std::unique_ptr<Pipeline>(new Pipeline());
    pipeline->InitWithGraph(graphutils::CreateGraphFromYaml(configPath));
    return pipeline;
}

// --- Public methods ---
Pipeline::Pipeline() : m_pImpl(std::make_unique<Pipeline::Impl>()) {}

Pipeline::~Pipeline() = default;

ErrorCode Pipeline::Init() {
    if (!m_pImpl) return ErrorCode::UNINITIALIZED_ERROR;

    for (auto& actorNode : m_pImpl->GetActorOrderedNodes()) {
        ErrorCode errCode = actorNode->Init();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Init module failed, actorName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Init module success, actorName={}", actorNode->GetModuleName());
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
    auto& actorOrderedNodes = m_pImpl->GetActorOrderedNodes();
    for (auto it = actorOrderedNodes.rbegin(); it != actorOrderedNodes.rend(); ++it) {
        auto& actorNode = *it;
        ErrorCode errCode = actorNode->DeInit();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("DeInit module failed, actorName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("DeInit module success, actorName={}", actorNode->GetModuleName());
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

    for (auto& actorNode : m_pImpl->GetActorOrderedNodes()) {
        ErrorCode errCode = actorNode->Start();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Start worker failed, actorName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Start module success, actorName={}", actorNode->GetModuleName());
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

    m_pImpl->StopQueues();

    ErrorCode errCode = ErrorCode::SUCCESS;
    for (auto& actorNode : m_pImpl->GetActorOrderedNodes()) {
        errCode = actorNode->Stop();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Stop worker failed, actorName={}", actorNode->GetModuleName());
            return errCode;
        } else {
            LOG_DEBUG("Stop module success, actorName={}", actorNode->GetModuleName());
        }
    }
    LOG_DEBUG("Pipeline stopped successfully.");
    return ErrorCode::SUCCESS;
}

// --- Private methods ---
void Pipeline::InitWithGraph(std::unique_ptr<Graph> graph) {
    LOG_DEBUG("Initializing pipeline with graph, graph={}", graph->toString());
    m_pImpl->Init(graph); // Init with graph
}

}; // namespace nexusflow
