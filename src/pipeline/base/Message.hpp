#pragma once

#include <mutex>
#include <sstream>
#include <string>
#include <vector>

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
        oss << "SeqMessage: " << m_dataVec.size() << " elements";
        oss << " [";
        for (const auto& str : m_dataVec) {
            oss << str << ", ";
        }
        oss << "]";
        return oss.str();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::string> m_dataVec;
};
