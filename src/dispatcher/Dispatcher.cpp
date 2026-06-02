#include "Dispatcher.hpp"
#include "nexusflow/Config.hpp"

namespace nexusflow { namespace dispatcher {

Dispatcher::Dispatcher() {};

Dispatcher::~Dispatcher() = default;

void Dispatcher::Broadcast(const Message& message, bool blocking) {
    for (auto& pair : m_subscriberMap) {
        auto& subscriber = pair.second;
        if (blocking) {
            subscriber->Push(message);
        } else {
            subscriber->TryPush(message);
        }
    }
}

void Dispatcher::SendTo(const std::string& outputName, const Message& msg, bool blocking) {
    auto it = m_subscriberMap.find(outputName);
    if (it != m_subscriberMap.end()) {
        auto& subscriber = it->second;
        if (blocking) {
            subscriber->Push(msg);
        } else {
            subscriber->TryPush(msg);
        }
    }
}

}} // namespace nexusflow::dispatcher