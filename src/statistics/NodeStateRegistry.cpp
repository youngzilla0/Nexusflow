#include "statistics/NodeStateRegistry.hpp"

#include "utils/logging.hpp"

#include <stdexcept>
#include <utility>

namespace nexusflow { namespace executor {

/**
 * @brief 注册一个节点的执行期状态并同步登记其统计状态。
 * @param nodeName 节点名称。
 * @param module 节点对应的模块实例。
 * @param runtimeConfig 节点运行时配置。
 * @param statistics 统计聚合器。
 * @param isSinkNode 当前节点是否为 sink 节点。
 */
void NodeStateRegistry::RegisterNode(const std::string& nodeName,
                                     const std::shared_ptr<Module>& module,
                                     const PipelineConfig& runtimeConfig,
                                     Statistics& statistics,
                                     bool isSinkNode) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_nodeStates.find(nodeName) != m_nodeStates.end()) {
        LOG_ERROR("Node with name '{}' already exists", nodeName);
        throw std::invalid_argument("Node with name " + nodeName + " already exists");
    }

    auto state = std::make_shared<NodeState>();
    state->nodeName = nodeName;
    state->module = module;
    state->runtimeConfig = runtimeConfig;
    state->isSinkNode = isSinkNode;
    auto registeredState = state;
    m_nodeStates.emplace(nodeName, std::move(state));

    statistics.RegisterNode(
        nodeName, registeredState->stats,
        [state = std::move(registeredState)]() -> std::uint64_t {
            return state->joinState.PendingGroupCount();
        },
        isSinkNode);
}

/**
 * @brief 为目标节点追加一个输入端口绑定。
 * @param nodeName 目标节点名称。
 * @param inputPortName 输入端口名称。
 * @param queue 端口对应的消息队列。
 * @param stats 该输入边对应的边级统计状态。
 */
void NodeStateRegistry::AddInputQueue(const std::string& nodeName,
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

/**
 * @brief 查找指定节点的执行期状态。
 * @param nodeName 节点名称。
 * @return 若存在则返回对应状态，否则返回空指针。
 */
NodeStateRegistry::NodeStatePtr NodeStateRegistry::Find(const std::string& nodeName) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_nodeStates.find(nodeName);
    if (it == m_nodeStates.end()) {
        return nullptr;
    }
    return it->second;
}

/**
 * @brief 返回当前全部节点状态的稳定快照。
 * @return 全部节点状态列表。
 */
std::vector<NodeStateRegistry::NodeStatePtr> NodeStateRegistry::SnapshotStates() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    std::vector<NodeStatePtr> states;
    states.reserve(m_nodeStates.size());
    for (const auto& entry : m_nodeStates) {
        states.push_back(entry.second);
    }
    return states;
}

/**
 * @brief 返回当前已注册节点数量。
 * @return 节点数量。
 */
std::size_t NodeStateRegistry::Size() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_nodeStates.size();
}

}} // namespace nexusflow::executor
