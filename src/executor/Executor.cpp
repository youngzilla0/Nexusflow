#include "executor/Executor.hpp"
#include "nexusflow/Config.hpp"
#include <thread>

namespace nexusflow { namespace executor {

Executor::Executor() : m_threadPool(std::make_unique<ThreadPool>(std::thread::hardware_concurrency())) {}

Executor::~Executor() = default;

void Executor::AddSubscriber(const std::string& name, ViewPtr<MessageQueue> queue) {
    if (m_subscriberMap.find(name) != m_subscriberMap.end()) {
        LOG_ERROR("Subscriber with name {} already exists", name);
        throw std::invalid_argument("Subscriber with name " + name + " already exists");
    }
    m_subscriberMap[name] = queue;
}

void Executor::Emit(const Message& message, bool blocking) {
    if (blocking) {
        for (auto& pair : m_subscriberMap) {
            pair.second->Push(message);
        }
    } else {
        // Async: submit dispatch task to thread pool for parallel fan-out
        m_threadPool->Submit([this, msg = message]() { DispatchTask(msg); });
    }
}

void Executor::Route(const std::string& outputName, const Message& msg, bool blocking) {
    auto it = m_subscriberMap.find(outputName);
    if (it == m_subscriberMap.end()) {
        return; // Silently ignore unknown output names
    }

    if (blocking) {
        it->second->Push(msg);
    } else {
        auto subscriber = it->second;
        m_threadPool->Submit([subscriber, msg = msg]() { subscriber->TryPush(msg); });
    }
}

void Executor::DispatchTask(const Message& msg) {
    for (auto& pair : m_subscriberMap) {
        pair.second->TryPush(msg);
    }
}

void Executor::Start() { m_threadPool->Start(); }

void Executor::Stop() { m_threadPool->Stop(); }

}} // namespace nexusflow::executor