#include "Dispatcher.hpp"

namespace nexusflow { namespace dispatcher {

Dispatcher::Dispatcher() = default;

Dispatcher::~Dispatcher() = default;

void Dispatcher::Broadcast(const Message& message, bool clone) {
    for (auto& pair : m_subscriberMap) {
        auto& subscriber = pair.second;
        auto messageClone = clone ? message.Clone() : message;
        subscriber->push(messageClone);
    }
}

void Dispatcher::SendTo(const std::string& outputName, const Message& msg, bool clone) {
    auto it = m_subscriberMap.find(outputName);
    if (it != m_subscriberMap.end()) {
        auto& subscriber = it->second;
        auto messageClone = clone ? msg.Clone() : msg;
        subscriber->push(messageClone);
    }
}

}} // namespace nexusflow::dispatcher