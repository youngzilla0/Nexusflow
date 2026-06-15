#ifndef NEXUSFLOW_ERROR_HPP
#define NEXUSFLOW_ERROR_HPP

#include <string>
#include <utility>

namespace nexusflow {

/**
 * @brief Unified framework error object.
 *
 * Error carries both an error code and a message:
 * - Ok() represents success
 * - Err() represents failure
 *
 * The same type is used for lifecycle return values and event reporting.
 */
class Error {
public:
    /**
     * @brief Framework-wide error codes.
     */
    enum class Code {
        Success = 0,
        Failure,
        AlreadyStarted,
        AlreadyStopped,
        WorkerStartFailed,
        WorkerStopFailed,
        Uninitialized,
        FileOpenFailed,
    };

    Error() = default;

    /**
     * @brief Create a successful result.
     * @param message Optional success note.
     */
    static Error Ok(std::string message = "") {
        Error err;
        err.m_code = Code::Success;
        err.m_message = std::move(message);
        return err;
    }

    /**
     * @brief Create a failure result.
     * @param code Error code.
     * @param message Error message.
     */
    static Error Err(Code code, std::string message) {
        Error err;
        err.m_code = code;
        err.m_message = std::move(message);
        return err;
    }

    /**
     * @brief Return whether this object represents success.
     */
    bool IsOk() const { return m_code == Code::Success; }

    /**
     * @brief Return whether this object represents failure.
     */
    bool IsErr() const { return !IsOk(); }

    /**
     * @brief Get the stored error code.
     */
    Code GetCode() const { return m_code; }

    /**
     * @brief Get the stored message.
     */
    const std::string& GetMessage() const { return m_message; }

    /**
     * @brief Allow Error to be used as a boolean value.
     */
    explicit operator bool() const { return IsOk(); }

    bool operator==(const Error& other) const { return m_code == other.m_code && m_message == other.m_message; }
    bool operator!=(const Error& other) const { return !(*this == other); }
    bool operator==(Code code) const { return m_code == code; }
    bool operator!=(Code code) const { return m_code != code; }

private:
    Code m_code = Code::Success;
    std::string m_message;
};

} // namespace nexusflow

#endif // NEXUSFLOW_ERROR_HPP
