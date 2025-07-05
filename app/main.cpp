#include "core/PipelineManager.hpp"
#include "helper/logging.hpp"
#include <chrono>

int main(int argc, char* argv[]) {
    // Initialize the global logger
    helper::logger::LoggerParam loggerParam;
    loggerParam.logLevel = helper::logger::LogLevel::TRACE;

    helper::logger::InitializeGlobalLogger(std::move(loggerParam));

    auto& pipelineMgr = pipeline_core::PipelineManager::GetInstance();

    auto pipelinePtr = pipelineMgr.CreatePipelineMock();

    if (pipelinePtr == nullptr) {
        LOG_ERROR("Failed to create pipeline");
        return -1;
    }

    LOG_INFO("Pipeline created, initializing...");
    pipelinePtr->Init();

    LOG_INFO("Pipeline initialized, starting...");
    pipelinePtr->Start();

    LOG_INFO("Pipeline running, waiting for 10 seconds...");
    std::this_thread::sleep_for(std::chrono::seconds(10));

    LOG_INFO("Pipeline stopping...");
    pipelinePtr->Stop();

    LOG_INFO("Pipeline stopped, deinitializing...");
    pipelinePtr->DeInit();

    LOG_INFO("Pipeline stopped, exiting...");

    return 0;
}