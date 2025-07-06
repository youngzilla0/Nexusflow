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

struct SeqMessage : public Message {
    void addData(const std::string& data) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_dataVec.push_back(data);
    }

    std::string toString() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::ostringstream oss;
        oss << "SeqMessage: {size=" << m_dataVec.size() << ", elements=[";
        for (size_t idx = 0; idx < m_dataVec.size(); ++idx) {
            oss << m_dataVec[idx];
            if (idx < m_dataVec.size() - 1) {
                oss << ", ";
            }
        }
        oss << "]}";
        return oss.str();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_dataVec;
};

} // namespace nexusflow

#endif // NEXUSFLOW_MESSAGE_HPP