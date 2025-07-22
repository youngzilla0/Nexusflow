#include "nexusflow/ProcessingContext.hpp"
#include "utils/logging.hpp"
#include <algorithm>

namespace nexusflow {

// constructor
ProcessingContext::ProcessingContext(Message singleInput)
    : m_inputPayload(std::move(singleInput)), m_inputPayloadType(InputPayloadType::SINGLE) {}

ProcessingContext::ProcessingContext(MessageHashMap multiInput)
    : m_inputPayload(std::move(multiInput)), m_inputPayloadType(InputPayloadType::MULTI) {}

const Message* ProcessingContext::GetInput() const noexcept {
    m_mode = ProcessingMode::BORROW_ONLY;
    return m_inputPayload.get<const Message>();
}

const Message* ProcessingContext::GetInput(const std::string& tag) const noexcept {
    if (auto* msgMapPtr = m_inputPayload.get<MessageHashMap>()) {
        auto it = msgMapPtr->find(tag);
        if (it != msgMapPtr->end()) {
            m_mode = ProcessingMode::BORROW_ONLY;
            return &it->second;
        }
    }
    return nullptr;
}

Message ProcessingContext::TakeInput() {
    if (m_inputPayloadType != InputPayloadType::SINGLE) {
        throw std::runtime_error("TakeInput() Input payload is not a single message");
    }

    auto* msg = m_inputPayload.get<Message>();
    if (msg == nullptr) {
        throw std::runtime_error("TakeInput() Input payload is null");
    }

    m_mode = ProcessingMode::TAKE_AND_OUTPUT;
    return std::move(*msg);
}

Message ProcessingContext::TakeInput(const std::string& tag) {
    if (m_inputPayloadType != InputPayloadType::MULTI) {
        throw std::runtime_error("TakeInput(tag) Input payload is not a multi message");
    }

    auto* msgMapPtr = m_inputPayload.get<MessageHashMap>();
    if (msgMapPtr == nullptr) {
        throw std::runtime_error("TakeInput(tag) Input payload is null");
    }

    auto it = msgMapPtr->find(tag);
    if (it == msgMapPtr->end()) {
        throw std::runtime_error("TakeInput(tag) Input payload does not contain tag: " + tag);
    }

    m_mode = ProcessingMode::TAKE_AND_OUTPUT;

    return std::move(it->second);
}

ProcessingContext::MessageVec ProcessingContext::CollectOutputs() {
    if (m_mode != ProcessingMode::MODIFY_IN_PLACE) {
        // The user did not modify the input in-place. The output is the user's output messages.
        // In all other cases (Borrow, Take, or no interaction), the output is
        // whatever the user explicitly added via AddOutput().
        return std::move(m_outputMessageVec);
    }

    // If the user modified the input in-place, we need to return the modified input.
    // The user modified the input in-place. The output is that single, modified message.
    // We can be certain the Any holds a Message* because MutInput() enforces it.

    if (m_inputPayloadType == InputPayloadType::SINGLE) {
        Message* modifiedInput = m_inputPayload.get<Message>();
        if (modifiedInput == nullptr) {
            throw std::runtime_error("CollectOutputs() Input payload is null");
        }
        return {std::move(*modifiedInput)};
    } else if (m_inputPayloadType == InputPayloadType::MULTI) {
        MessageHashMap* modifiedInput = m_inputPayload.get<MessageHashMap>();
        if (modifiedInput == nullptr) {
            throw std::runtime_error("CollectOutputs() Input payload is null");
        }
        MessageVec modifiedInputVec;
        modifiedInputVec.reserve(modifiedInput->size());
        for (auto& pair : *modifiedInput) {
            modifiedInputVec.push_back(std::move(pair.second));
        }
        return modifiedInputVec;
    } else {
        throw std::runtime_error("CollectOutputs() Input payload is not a single or multi message");
    }
}

void ProcessingContext::AddOutput(Message msg) { m_outputMessageVec.push_back(std::move(msg)); }

} // namespace nexusflow