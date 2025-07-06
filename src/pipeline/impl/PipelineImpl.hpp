#ifndef NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
#define NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP

#include "base/Define.hpp"
#include "base/Graph.hpp"
#include "core/Worker.hpp"
#include "dispatcher/Dispatcher.hpp"
#include <nexusflow/Pipeline.hpp>
#include <string>

namespace nexusflow {

// Forward declarations
class Pipeline;

// An internal struct to group all runtime components related to a single module.
using ActiveName = std::string;
class ActiveNode {
public:
    ActiveNode(const std::shared_ptr<Module>& module) {
        m_module = module;
        m_worker = std::make_shared<core::Worker>(m_module);
        m_dispatcher = std::make_shared<dispatcher::Dispatcher>();
    }

    void AddInputQueue(const std::string& name, ViewPtr<MessageQueue> queue) { m_worker->AddQueue(name, queue); }

    void AddOutputQueue(const std::string& name, ViewPtr<MessageQueue> queue) { m_dispatcher->AddSubscriber(name, queue); }

    std::shared_ptr<Module>& GetModule() { return m_module; };
    std::shared_ptr<core::Worker>& GetWorker() { return m_worker; }
    std::shared_ptr<dispatcher::Dispatcher>& GetDispatcher() { return m_dispatcher; }

private:
    std::shared_ptr<Module> m_module;
    std::shared_ptr<core::Worker> m_worker;
    std::shared_ptr<dispatcher::Dispatcher> m_dispatcher;
};

// --- Pipeline's Private Implementation (m_pImpl) ---
class Pipeline::Impl {
public:
    std::unique_ptr<Graph> graph;
    std::vector<MessageQueueUPtr> queues;
    std::unordered_map<ActiveName, std::shared_ptr<ActiveNode>> activeNodeMap;

    ErrorCode Init();

private:
    std::shared_ptr<ActiveNode> GetOrCreateActiveNode(const std::shared_ptr</*Graph::*/ Node>& node);
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_PIPELINEIMPL_HPP
