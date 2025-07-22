#ifndef NEXUSFLOW_PROCESSING_CONTEXT_HPP
#define NEXUSFLOW_PROCESSING_CONTEXT_HPP

#include <map>
#include <nexusflow/Any.hpp>
#include <nexusflow/Message.hpp>
#include <sstream>
#include <unordered_map>

namespace nexusflow {

// --- Return Status ---
enum class ProcessStatus { OK = 0, ERROR, FAILED_GET_INPUT };

// --- I/O Context ---
namespace core {
class Worker;
}

enum class InputPayloadType {
    SINGLE, // The payload is a single Message
    MULTI // The payload is a map<string, Message>
};

/**
 * @class ProcessingContext
 * @brief Manages the I/O for a single call to a Module's Process() method.
 *
 * This class acts as the primary interface for a module to interact with the
 * pipeline during its execution. It provides a safe and ergonomic API for
 * modules to get input messages and produce output messages.
 *
 * It is a transient object, created by a Worker for each processing cycle
 * and destroyed immediately after.
 */
class ProcessingContext {
    using MessageHashMap = std::unordered_map<std::string, Message>;
    using MessageVec = std::vector<Message>;

public:
    // --- Message-Level API ---
    // For when you need access to the Message object itself (e.g., for metadata).

    /**
     * @brief Gets a const pointer to the single input Message.
     */
    const Message* GetInput() const noexcept;

    /**
     * @brief Gets a const pointer to a tagged input Message.
     * @param tag The tag of the input message.
     */
    const Message* GetInput(const std::string& tag) const noexcept;

    /**
     * @brief Takes ownership of the single input Message.
     */
    Message TakeInput() noexcept(false);

    /**
     * @brief Takes ownership of a tagged input Message.
     * @param tag The tag of the input message.
     */
    Message TakeInput(const std::string& tag) noexcept(false);

    // --- Payload-Level API ---
    // Convenience methods for direct access to the data inside the Message.

    /**
     * @brief Borrows a const pointer to the payload of the single input Message.
     */
    template <typename T>
    const T* BorrowPayload() const noexcept;

    /**
     * @brief Borrows a const pointer to the payload of a tagged input Message.
     * @param tag The tag of the input message.
     */
    template <typename T>
    const T* BorrowPayload(const std::string& tag) const noexcept;

    /**
     * @brief Gets a mutable pointer to the payload of the single input Message for in-place modification.
     *        Signals to the framework that the input should become the output.
     */
    template <typename T>
    T* MutPayload() noexcept;

    /**
     * @brief Gets a mutable pointer to the payload of a tagged input Message.
     * @param tag The tag of the input message.
     */
    template <typename T>
    T* MutPayload(const std::string& tag) noexcept;

    /**
     * @brief Adds an output message to the context.
     * @param msg The message to add.
     */
    void AddOutput(Message msg);

private:
    // Only the Worker can create a ProcessingContext.
    friend class core::Worker;

    // --- Worker-Facing API ---
    ProcessingContext(Message singleInput);
    ProcessingContext(MessageHashMap multiInput);

    // Collects the final output messages based on the module's interaction.
    MessageVec CollectOutputs();

    // The context is non-copyable and non-movable as it's a transient view.
    // ProcessingContext(const ProcessingContext&) = delete;
    // ProcessingContext& operator=(const ProcessingContext&) = delete;
    // ProcessingContext(ProcessingContext&&) = delete;
    // ProcessingContext& operator=(ProcessingContext&&) = delete;

private:
    // --- Internal State ---
    enum class ProcessingMode {
        UNSPECIFIED, // No input operation has occurred yet.
        BORROW_ONLY, // User has only borrowed input(s).
        TAKE_AND_OUTPUT, // User has taken input(s) for manual output.
        MODIFY_IN_PLACE // User has used MutInput() for in-place modification.
    };

    Any m_inputPayload;
    const InputPayloadType m_inputPayloadType;
    mutable ProcessingMode m_mode = ProcessingMode::UNSPECIFIED;
    MessageVec m_outputMessageVec;
};

// --- Template Method Implementation ---
template <typename T>
const T* ProcessingContext::BorrowPayload() const noexcept {
    if (auto* msgPtr = m_inputPayload.get<Message>()) {
        return msgPtr->BorrowPtr<T>();
    }
    return nullptr;
}

template <typename T>
const T* ProcessingContext::BorrowPayload(const std::string& tag) const noexcept {
    if (auto* msgMapPtr = m_inputPayload.get<MessageHashMap>()) {
        auto it = msgMapPtr->find(tag);
        if (it != msgMapPtr->end()) {
            m_mode = ProcessingMode::BORROW_ONLY;
            return it->second.BorrowPtr<T>();
        }
    }
    return nullptr;
}

template <typename T>
T* ProcessingContext::MutPayload() noexcept {
    if (auto* msgPtr = m_inputPayload.get<Message>()) {
        auto* ptr = msgPtr->MutPtr<T>();
        if (ptr) m_mode = ProcessingMode::MODIFY_IN_PLACE;
        return ptr;
    }
    return nullptr;
}

template <typename T>
T* ProcessingContext::MutPayload(const std::string& tag) noexcept {
    if (auto* msgMapPtr = m_inputPayload.get<MessageHashMap>()) {
        auto it = msgMapPtr->find(tag);
        if (it != msgMapPtr->end()) {
            auto* ptr = it->second.MutPtr<T>();
            if (ptr) m_mode = ProcessingMode::MODIFY_IN_PLACE;
            return ptr;
        }
    }
    return nullptr;
}

} // namespace nexusflow

#endif