
#include "core/Pipeline.hpp"
#include "ErrorCode.hpp"
#include "base/ConcurrentQueue.hpp"
#include "base/Message.hpp"
#include "core/ModuleBase.hpp"
#include "helper/logging.hpp"
#include "modules/ModuleFactory.hpp"
#include <memory>
#include <string>
#include <unordered_map>

namespace pipeline_core {

std::shared_ptr<ModuleBase> Pipeline::getOrCreateModule(const std::string& name, const ModuleName& moduleName) {
    auto& moduleMap = m_moduleMap;
    if (moduleMap.find(name) != moduleMap.end()) {
        LOG_DEBUG("Port '{}' already exists in pipeline. Reusing it.", name);
        return moduleMap[name];
    }

    LOG_TRACE("Port '{}' not found in pipeline. Creating a new instance.", name);
    std::shared_ptr<ModuleBase> newModule = ModuleFactory::getInstance().CreateModule(moduleName);

    if (!newModule) {
        LOG_WARN("Failed to create module '{}'. Aborting connection.", moduleName);
        return nullptr;
    }

    moduleMap[name] = newModule;

    return newModule;
}

std::unique_ptr<Pipeline> Pipeline::makeByGraph(const Graph& graph) {
    auto pipeline = std::unique_ptr<Pipeline>(new Pipeline());

    auto edgeList = graph.toEdgeListBFS();

    auto& moduleFactory = ModuleFactory::getInstance();

    LOG_INFO("edgeList size: {}", edgeList.size());

    for (size_t idx = 0; idx < edgeList.size(); ++idx) {
        // 1. create module by graph.node

        auto edge = edgeList[idx];
        auto srcNodeWPtr = edge.srcNodePtr;
        auto dstNodeWPtr = edge.dstNodePtr;

        auto srcNode = srcNodeWPtr.lock();
        auto dstNode = dstNodeWPtr.lock();

        if (srcNode == nullptr || dstNode == nullptr) {
            LOG_ERROR("srcNode or dstNode is nullptr");
            // throw std::runtime_error("srcNode or dstNode is nullptr");
            continue;
        }

        auto srcNodeName = srcNode->name;
        auto dstNodeName = dstNode->name;

        auto srcModuleName = srcNode->moduleName;
        auto dstModuleName = dstNode->moduleName;

        auto srcModule = pipeline->getOrCreateModule(srcNodeName, srcModuleName);
        auto dstModule = pipeline->getOrCreateModule(dstNodeName, dstModuleName);

        // 2. connect module by set module input/output

        constexpr int kQueueSize = 5;
        auto queue = std::make_shared<ConcurrentQueue<std::shared_ptr<Message>>>(kQueueSize);

        std::string portName = srcNodeName + "_TO_" + dstNodeName;

        srcModule->addOutputPort(portName, queue);
        dstModule->addInputPort(portName, queue);
    }

    return pipeline;
}

ErrorCode Pipeline::Init() {
    ErrorCode errCode = ErrorCode::SUCCESS;
    for (auto& item : m_moduleMap) {
        auto& moduleName = item.first;
        auto& moduleInst = item.second;
        errCode = moduleInst->Init();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("Init module {} failed", moduleName);
            return errCode;
        }
    }
    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::DeInit() {
    ErrorCode errCode = ErrorCode::SUCCESS;
    for (auto& item : m_moduleMap) {
        auto& moduleName = item.first;
        auto& moduleInst = item.second;
        errCode = moduleInst->DeInit();
        if (errCode != ErrorCode::SUCCESS) {
            LOG_ERROR("DeInit module {} failed", moduleName);
            return errCode;
        }
    }
    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::Start() {
    ErrorCode errCode = ErrorCode::SUCCESS;
    for (auto& item : m_moduleMap) {
        auto& moduleName = item.first;
        auto& moduleInst = item.second;
        moduleInst->Start();
    }
    return ErrorCode::SUCCESS;
}

ErrorCode Pipeline::Stop() {
    ErrorCode errCode = ErrorCode::SUCCESS;
    for (auto& item : m_moduleMap) {
        auto& moduleName = item.first;
        auto& moduleInst = item.second;
        moduleInst->Stop();
    }
    return ErrorCode::SUCCESS;
}

}; // namespace pipeline_core
