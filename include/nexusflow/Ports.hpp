#ifndef NEXUSFLOW_PORTS_HPP
#define NEXUSFLOW_PORTS_HPP

#include <nexusflow/Message.hpp>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace nexusflow {

namespace executor {
class Executor;
}

using PortName = std::string;

static constexpr const char* kDefaultInputPort = "in";
static constexpr const char* kDefaultOutputPort = "out";

struct PortMessage {
    PortName port;
    Message message;
};

class PortInputsView {
public:
    PortInputsView() : m_messages(nullptr) {}
    explicit PortInputsView(const std::vector<PortMessage>& messages) : m_messages(&messages) {}

    bool Empty() const { return Size() == 0; }

    size_t Size() const { return m_messages == nullptr ? 0 : m_messages->size(); }

    bool Has(const std::string& port) const { return FindPortMessage(port) != nullptr; }

    const PortMessage* FindPortMessage(const std::string& port) const {
        if (m_messages == nullptr) return nullptr;
        for (const auto& item : *m_messages) {
            if (item.port == port) {
                return &item;
            }
        }
        return nullptr;
    }

    const Message* FindMessage(const std::string& port) const {
        auto* portMessage = FindPortMessage(port);
        return portMessage == nullptr ? nullptr : &portMessage->message;
    }

    const Message& GetMessage(const std::string& port) const {
        auto* message = FindMessage(port);
        if (message == nullptr) {
            throw std::out_of_range("Port '" + port + "' is not available in PortInputsView");
        }
        return *message;
    }

    template <typename T>
    const T* Get(const std::string& port) const {
        auto* message = FindMessage(port);
        return message == nullptr ? nullptr : message->BorrowPtr<T>();
    }

    const PortMessage* Only() const { return Size() == 1 ? &(*m_messages)[0] : nullptr; }

    const Message* OnlyMessage() const {
        auto* portMessage = Only();
        return portMessage == nullptr ? nullptr : &portMessage->message;
    }

    const std::string* ActivePort() const {
        auto* portMessage = Only();
        return portMessage == nullptr ? nullptr : &portMessage->port;
    }

    template <typename T>
    const T* OnlyAs() const {
        auto* message = OnlyMessage();
        return message == nullptr ? nullptr : message->BorrowPtr<T>();
    }

private:
    const std::vector<PortMessage>* m_messages;
};

class PortOutputs {
public:
    void Emit(const Message& message, bool blocking) { m_broadcasts.push_back(BroadcastCommand{message, blocking}); }

    void Emit(Message&& message, bool blocking) {
        m_broadcasts.push_back(BroadcastCommand{std::move(message), blocking});
    }

    void Set(const std::string& port, const Message& message, bool blocking) {
        m_routes.push_back(RouteCommand{PortMessage{port, message}, blocking});
    }

    void Set(const std::string& port, Message&& message, bool blocking) {
        m_routes.push_back(RouteCommand{PortMessage{port, std::move(message)}, blocking});
    }

    bool Empty() const { return m_broadcasts.empty() && m_routes.empty(); }

private:
    friend class executor::Executor;

    struct BroadcastCommand {
        Message message;
        bool blocking = false;
    };

    struct RouteCommand {
        PortMessage portMessage;
        bool blocking = true;
    };

    std::vector<BroadcastCommand> m_broadcasts;
    std::vector<RouteCommand> m_routes;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PORTS_HPP
