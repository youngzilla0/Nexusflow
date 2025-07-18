#include "Dispatcher.hpp"
#include "nexusflow/Config.hpp"

namespace nexusflow { namespace dispatcher {

Dispatcher::Dispatcher(const ViewPtr<Config>& configView) { m_configView = configView; };

Dispatcher::~Dispatcher() = default;

void Dispatcher::Broadcast(const Message& message) {
    bool clone = false;
    clone = m_configView->GetValueOrDefault("cloneMessage", false);

    for (auto& pair : m_subscriberMap) {
        auto& subscriber = pair.second;
        auto messageClone = clone ? message.Clone() : message;
        subscriber->tryPush(messageClone);
    }
}

void Dispatcher::SendTo(const std::string& outputName, const Message& msg) {
    bool clone = false;
    clone = m_configView->GetValueOrDefault("cloneMessage", false);

    auto it = m_subscriberMap.find(outputName);
    if (it != m_subscriberMap.end()) {
        auto& subscriber = it->second;
        auto messageClone = clone ? msg.Clone() : msg;
        subscriber->tryPush(messageClone);
    }
}

}} // namespace nexusflow::dispatcher