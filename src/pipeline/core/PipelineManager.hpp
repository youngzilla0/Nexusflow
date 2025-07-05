#ifndef PIPELINE_MGR_HPP_
#define PIPELINE_MGR_HPP_

#include "ErrorCode.hpp"
#include "Pipeline.hpp"
#include "base/Graph.hpp"

namespace pipeline_core {

class PipelineManager {
public:
    static PipelineManager& GetInstance() {
        static PipelineManager instance;
        return instance;
    }

    ~PipelineManager();

    std::unique_ptr<Pipeline> CreatePipeline(const Graph& graph);

    std::unique_ptr<Pipeline> CreatePipelineMock();

private:
    PipelineManager() = default;
};

}; // namespace pipeline_core

#endif