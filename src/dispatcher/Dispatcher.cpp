#include "Dispatcher.hpp"

namespace nexusflow { namespace dispatcher {

Dispatcher::Dispatcher() = default;

Dispatcher::~Dispatcher() = default;

void Dispatcher::Broadcast(const MessagePtr& message) {
    for (auto& pair : m_subscriberMap) {
        auto& subscriber = pair.second;
        subscriber->push(message);
    }
}

void Dispatcher::SendTo(const std::string& outputName, const MessagePtr& msg) {
    auto it = m_subscriberMap.find(outputName);
    if (it != m_subscriberMap.end()) {
        auto& subscriber = it->second;
        subscriber->push(msg);
    }
}

}} // namespace nexusflow::dispatcher