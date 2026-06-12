#include "executor/NodeRegistry.hpp"

#include "utils/logging.hpp"

#include <stdexcept>
#include <utility>

namespace nexusflow { namespace executor {

void NodeRegistry::RegisterNode(const std::string& nodeName,
                                const std::shared_ptr<Module>& module,
                                const PipelineConfig& runtimeConfig,
                                Statistics& statistics) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_nodeStates.find(nodeName) != m_nodeStates.end()) {
        LOG_ERROR("Node with name '{}' already exists", nodeName);
        throw std::invalid_argument("Node with name " + nodeName + " already exists");
    }

    auto state = std::make_shared<NodeState>();
    state->nodeName = nodeName;
    state->module = module;
    state->runtimeConfig = runtimeConfig;
    auto registeredState = state;
    m_nodeStates.emplace(nodeName, std::move(state));

    statistics.RegisterNode(
        nodeName, registeredState->stats,
        [state = std::move(registeredState)]() -> std::uint64_t {
            return state->joinState.PendingGroupCount();
        });
}

void NodeRegistry::AddInputQueue(const std::string& nodeName,
                                 const std::string& inputPortName,
                                 ViewPtr<MessageQueue> queue,
                                 const PortStatsStatePtr& stats) {
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_nodeStates.find(nodeName);
    if (it == m_nodeStates.end()) {
        throw std::invalid_argument("Unknown node " + nodeName);
    }

    it->second->inputQueues.push_back(InputQueueBinding{inputPortName, queue, stats});
}

NodeRegistry::NodeStatePtr NodeRegistry::Find(const std::string& nodeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_nodeStates.find(nodeName);
    if (it == m_nodeStates.end()) {
        return nullptr;
    }
    return it->second;
}

std::vector<NodeRegistry::NodeStatePtr> NodeRegistry::SnapshotStates() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<NodeStatePtr> states;
    states.reserve(m_nodeStates.size());
    for (const auto& entry : m_nodeStates) {
        states.push_back(entry.second);
    }
    return states;
}

std::size_t NodeRegistry::Size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_nodeStates.size();
}

}} // namespace nexusflow::executor
