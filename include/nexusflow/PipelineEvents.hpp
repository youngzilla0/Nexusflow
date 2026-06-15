#ifndef NEXUSFLOW_PIPELINE_EVENTS_HPP
#define NEXUSFLOW_PIPELINE_EVENTS_HPP

#include <nexusflow/ErrorCode.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace nexusflow {

/**
 * @brief Pipeline 生命周期错误事件。
 */
struct PipelineErrorEvent {
    std::string pipelineName;
    std::string stage;
    std::string nodeName;
    ErrorCode code = ErrorCode::SUCCESS;
    std::string message;
};

/**
 * @brief Pipeline 消息投递异常类型。
 */
enum class PipelineMessageEventType {
    Dropped,
    Rejected,
};

/**
 * @brief Pipeline 中消息被丢弃或拒绝时的事件。
 */
struct PipelineMessageEvent {
    std::string pipelineName;
    std::string srcNodeName;
    std::string srcPortName;
    std::string dstNodeName;
    std::string dstInputPortName;
    PipelineMessageEventType type = PipelineMessageEventType::Dropped;
    std::uint64_t affectedCount = 0;
    bool blocking = false;
    std::string reason;
};

/**
 * @brief Pipeline 事件观察接口。
 *
 * 该接口用于异步接收 Pipeline 生命周期和消息投递事件，
 * 与统计快照采集接口解耦。
 */
class IPipelineObserver {
public:
    virtual ~IPipelineObserver() = default;

    virtual void OnPipelineInitialized(const std::string&) {}
    virtual void OnPipelineStarted(const std::string&) {}
    virtual void OnPipelineStopped(const std::string&) {}
    virtual void OnPipelineDeInitialized(const std::string&) {}
    virtual void OnPipelineError(const PipelineErrorEvent&) {}
    virtual void OnMessageEvent(const PipelineMessageEvent&) {}
};

/**
 * @brief 回调式 Pipeline 事件观察器。
 */
class CallbackPipelineObserver : public IPipelineObserver {
public:
    CallbackPipelineObserver& OnInitialized(std::function<void(const std::string&)> callback) {
        m_onInitialized = std::move(callback);
        return *this;
    }

    CallbackPipelineObserver& OnStarted(std::function<void(const std::string&)> callback) {
        m_onStarted = std::move(callback);
        return *this;
    }

    CallbackPipelineObserver& OnStopped(std::function<void(const std::string&)> callback) {
        m_onStopped = std::move(callback);
        return *this;
    }

    CallbackPipelineObserver& OnDeInitialized(std::function<void(const std::string&)> callback) {
        m_onDeInitialized = std::move(callback);
        return *this;
    }

    CallbackPipelineObserver& OnError(std::function<void(const PipelineErrorEvent&)> callback) {
        m_onError = std::move(callback);
        return *this;
    }

    CallbackPipelineObserver& OnMessage(std::function<void(const PipelineMessageEvent&)> callback) {
        m_onMessage = std::move(callback);
        return *this;
    }

    void OnPipelineInitialized(const std::string& pipelineName) override {
        if (m_onInitialized) {
            m_onInitialized(pipelineName);
        }
    }

    void OnPipelineStarted(const std::string& pipelineName) override {
        if (m_onStarted) {
            m_onStarted(pipelineName);
        }
    }

    void OnPipelineStopped(const std::string& pipelineName) override {
        if (m_onStopped) {
            m_onStopped(pipelineName);
        }
    }

    void OnPipelineDeInitialized(const std::string& pipelineName) override {
        if (m_onDeInitialized) {
            m_onDeInitialized(pipelineName);
        }
    }

    void OnPipelineError(const PipelineErrorEvent& event) override {
        if (m_onError) {
            m_onError(event);
        }
    }

    void OnMessageEvent(const PipelineMessageEvent& event) override {
        if (m_onMessage) {
            m_onMessage(event);
        }
    }

private:
    std::function<void(const std::string&)> m_onInitialized;
    std::function<void(const std::string&)> m_onStarted;
    std::function<void(const std::string&)> m_onStopped;
    std::function<void(const std::string&)> m_onDeInitialized;
    std::function<void(const PipelineErrorEvent&)> m_onError;
    std::function<void(const PipelineMessageEvent&)> m_onMessage;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_EVENTS_HPP
