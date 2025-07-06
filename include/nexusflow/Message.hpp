#ifndef NEXUSFLOW_MESSAGE_HPP
#define NEXUSFLOW_MESSAGE_HPP

#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace nexusflow {

// Base class for all messages
struct Message {
    virtual ~Message() = default;

    virtual std::string toString() const = 0;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MESSAGE_HPP