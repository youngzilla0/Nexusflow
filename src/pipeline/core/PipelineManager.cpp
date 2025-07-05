#include "PipelineManager.hpp"
#include "Pipeline.hpp"

namespace pipeline_core {

PipelineManager::~PipelineManager() = default;

std::unique_ptr<Pipeline> PipelineManager::CreatePipeline(const Graph& graph) {
    //
    return Pipeline::makeByGraph(graph);
}

std::unique_ptr<Pipeline> PipelineManager::CreatePipelineMock() {
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

    return Pipeline::makeByGraph(graph);
}

} // namespace pipeline_core