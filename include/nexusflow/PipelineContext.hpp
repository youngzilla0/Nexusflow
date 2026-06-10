#ifndef NEXUSFLOW_PIPELINE_CONTEXT_HPP
#define NEXUSFLOW_PIPELINE_CONTEXT_HPP

#include <nexusflow/PipelineConfig.hpp>

#include <cstddef>
#include <string>

namespace nexusflow {

namespace executor {
class Executor;
}

class ModuleActor;
class Pipeline;

class PipelineContext {
public:
    const std::string& GetPipelineName() const { return m_pipelineName; }

    const PipelineConfig& GetConfig() const { return m_config; }

    size_t GetExecutorThreadCount() const { return m_executorThreadCount; }

    void SetExecutorThreadCount(size_t threadCount) { m_executorThreadCount = threadCount; }

private:
    friend class ModuleActor;
    friend class Pipeline;
    friend class executor::Executor;

    PipelineContext(std::string pipelineName, PipelineConfig config)
        : m_pipelineName(std::move(pipelineName)), m_config(std::move(config)) {}

    std::string m_pipelineName;
    PipelineConfig m_config;
    size_t m_executorThreadCount = 0;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_CONTEXT_HPP
