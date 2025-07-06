#include "PipelineManager.hpp"
#include "GraphUtils.hpp"
#include "base/Graph.hpp"
#include "helper/logging.hpp"

namespace pipeline_run {

PipelineManager::~PipelineManager() = default;

std::unique_ptr<pipeline_core::Pipeline> PipelineManager::CreatePipelineByYamlConfig(const std::string& configPath) {
    auto graphPtr = graphutils::loadGrapFromYaml(configPath);
    if (graphPtr == nullptr) {
        LOG_ERROR("Failed to load graph from yaml, configPath: {}", configPath);
        return nullptr;
    }
    auto pipelinePtr = pipeline_core::Pipeline::makeByGraph(*graphPtr);
    return pipelinePtr;
}

std::unique_ptr<pipeline_core::Pipeline> PipelineManager::CreatePipelineMock() {
    Graph graph;

    /**
     *             InputNode
     *            /         \
     *      ProcessNode1   ProcessNode2
     *            \         /
     *             OutputNode
     */

    auto inputNodePtr = std::make_shared<Node>("InputNode", "MockInputModule");
    auto processNode1Ptr = std::make_shared<Node>("ProcessNode1", "MockProcessModule");
    auto processNode2Ptr = std::make_shared<Node>("ProcessNode2", "MockProcessModule");
    auto outputNodePtr = std::make_shared<Node>("OutputNode", "MockOutputModule");

    graph.addEdge(inputNodePtr, processNode1Ptr);
    graph.addEdge(inputNodePtr, processNode2Ptr);
    graph.addEdge(processNode1Ptr, outputNodePtr);
    graph.addEdge(processNode2Ptr, outputNodePtr);

    graph.setInputNodePtr(inputNodePtr);
    graph.setOutputNodePtr(outputNodePtr);

    graph.setName("MockPipeline");

    if (graph.hasCycle()) {
        throw std::runtime_error("Graph has cycle");
    }

    return pipeline_core::Pipeline::makeByGraph(graph);
}

} // namespace pipeline_run