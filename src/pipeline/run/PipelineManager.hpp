#ifndef PIPELINE_MGR_HPP_
#define PIPELINE_MGR_HPP_

#include "ErrorCode.hpp"
#include "core/Pipeline.hpp"

namespace pipeline_run {

class PipelineManager {
public:
    static PipelineManager& GetInstance() {
        static PipelineManager instance;
        return instance;
    }

    ~PipelineManager();

    std::unique_ptr<pipeline_core::Pipeline> CreatePipelineByYamlConfig(const std::string& configPath);

    std::unique_ptr<pipeline_core::Pipeline> CreatePipelineMock();

private:
    PipelineManager() = default;
};

} // namespace pipeline_run

#endif