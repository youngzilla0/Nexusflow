#include "executor/PortRouter.hpp"

#include <utility>

namespace nexusflow { namespace executor {

PortRouter::PortRouter(bool statisticsEnabled,
                       QueueFullPolicyProvider queueFullPolicyProvider,
                       ReadyCallback readyCallback,
                       MessageEventCallback messageEventCallback)
    : m_statisticsEnabled(statisticsEnabled),
      m_queueFullPolicyProvider(std::move(queueFullPolicyProvider)),
      m_readyCallback(std::move(readyCallback)),
      m_messageEventCallback(std::move(messageEventCallback)) {}

void PortRouter::AddOutputQueue(const std::string& nodeName,
                                const std::string& outputPortName,
                                const std::string& dstNodeName,
                                const std::string& dstInputPortName,
                                ViewPtr<MessageQueue> queue,
                                const PortStatsStatePtr& stats) {
    OutputSubscriber subscriber{dstNodeName, dstInputPortName, queue, stats};
    m_broadcastSubscribers[nodeName].push_back(subscriber);
    m_outputSubscribers[MakeOutputKey(nodeName, outputPortName)].push_back(std::move(subscriber));
}

void PortRouter::Emit(const std::string& nodeName, const Message& message, bool blocking) {
    auto it = m_broadcastSubscribers.find(nodeName);
    if (it == m_broadcastSubscribers.end()) {
        return;
    }

    for (const auto& subscriber : it->second) {
        DispatchToSubscriber(subscriber, nodeName, kDefaultOutputPort, message, blocking);
    }
}

void PortRouter::Route(const std::string& nodeName, const std::string& outputPortName, const Message& message, bool blocking) {
    auto it = m_outputSubscribers.find(MakeOutputKey(nodeName, outputPortName));
    if (it == m_outputSubscribers.end()) {
        return;
    }

    for (const auto& subscriber : it->second) {
        DispatchToSubscriber(subscriber, nodeName, outputPortName, message, blocking);
    }
}

void PortRouter::SetMessageEventCallback(MessageEventCallback messageEventCallback) {
    m_messageEventCallback = std::move(messageEventCallback);
}

std::string PortRouter::MakeOutputKey(const std::string& nodeName, const std::string& outputPortName) {
    return nodeName + "\n" + outputPortName;
}

void PortRouter::DispatchToSubscriber(const OutputSubscriber& subscriber,
                                      const std::string& srcNodeName,
                                      const std::string& srcPortName,
                                      const Message& message,
                                      bool blocking) {
    if (m_statisticsEnabled && subscriber.stats != nullptr) {
        subscriber.stats->RecordPushAttempt(blocking);
    }

    if (blocking) {
        auto status = subscriber.queue->PushWithStatus(message);
        if (status == MessageQueue::PushStatus::Success) {
            if (m_statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushAccepted(1, 0);
            }
            if (m_readyCallback) {
                m_readyCallback(subscriber.dstNodeName);
            }
        } else {
            if (m_statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushRejected();
            }
            NotifyMessageEvent(subscriber, srcNodeName, srcPortName, PipelineMessageEventType::Rejected, 1, true, "queue shutdown");
        }
        return;
    }

    auto queueFullPolicy =
        m_queueFullPolicyProvider ? m_queueFullPolicyProvider() : QueueFullPolicy::DropTail;

    if (queueFullPolicy == QueueFullPolicy::DropHead) {
        auto result = subscriber.queue->TryPushDropHead(message);
        if (result.status == MessageQueue::PushStatus::Success) {
            if (m_statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushAccepted(1, result.droppedCount);
            }
            if (result.droppedCount > 0) {
                NotifyMessageEvent(subscriber,
                                   srcNodeName,
                                   srcPortName,
                                   PipelineMessageEventType::Dropped,
                                   result.droppedCount,
                                   false,
                                   "drop head overflow");
            }
            if (m_readyCallback) {
                m_readyCallback(subscriber.dstNodeName);
            }
        } else if (result.status == MessageQueue::PushStatus::Shutdown) {
            if (m_statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushRejected();
            }
            NotifyMessageEvent(subscriber, srcNodeName, srcPortName, PipelineMessageEventType::Rejected, 1, false, "queue shutdown");
        } else {
            if (m_statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushDropped(1);
            }
            NotifyMessageEvent(subscriber, srcNodeName, srcPortName, PipelineMessageEventType::Dropped, 1, false, "drop head fallback");
        }
        return;
    }

    auto status = subscriber.queue->TryPushWithStatus(message);
    if (status == MessageQueue::PushStatus::Success) {
        if (m_statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushAccepted(1, 0);
        }
        if (m_readyCallback) {
            m_readyCallback(subscriber.dstNodeName);
        }
    } else if (status == MessageQueue::PushStatus::Shutdown) {
        if (m_statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushRejected();
        }
        NotifyMessageEvent(subscriber, srcNodeName, srcPortName, PipelineMessageEventType::Rejected, 1, false, "queue shutdown");
    } else {
        if (m_statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushDropped(1);
        }
        NotifyMessageEvent(subscriber, srcNodeName, srcPortName, PipelineMessageEventType::Dropped, 1, false, "drop tail overflow");
    }
}

void PortRouter::NotifyMessageEvent(const OutputSubscriber& subscriber,
                                    const std::string& srcNodeName,
                                    const std::string& srcPortName,
                                    PipelineMessageEventType type,
                                    std::uint64_t affectedCount,
                                    bool blocking,
                                    const std::string& reason) const {
    if (!m_messageEventCallback) {
        return;
    }

    PipelineMessageEvent event;
    event.srcNodeName = srcNodeName;
    event.srcPortName = srcPortName;
    event.dstNodeName = subscriber.dstNodeName;
    event.dstInputPortName = subscriber.dstInputPortName;
    event.type = type;
    event.affectedCount = affectedCount;
    event.blocking = blocking;
    event.reason = reason;
    m_messageEventCallback(event);
}

}} // namespace nexusflow::executor
