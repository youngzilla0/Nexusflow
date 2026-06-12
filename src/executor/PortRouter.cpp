#include "executor/PortRouter.hpp"

#include <utility>

namespace nexusflow { namespace executor {

PortRouter::PortRouter(bool statisticsEnabled, QueueFullPolicyProvider queueFullPolicyProvider, ReadyCallback readyCallback)
    : m_statisticsEnabled(statisticsEnabled),
      m_queueFullPolicyProvider(std::move(queueFullPolicyProvider)),
      m_readyCallback(std::move(readyCallback)) {}

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
        DispatchToSubscriber(subscriber, message, blocking);
    }
}

void PortRouter::Route(const std::string& nodeName, const std::string& outputPortName, const Message& message, bool blocking) {
    auto it = m_outputSubscribers.find(MakeOutputKey(nodeName, outputPortName));
    if (it == m_outputSubscribers.end()) {
        return;
    }

    for (const auto& subscriber : it->second) {
        DispatchToSubscriber(subscriber, message, blocking);
    }
}

std::string PortRouter::MakeOutputKey(const std::string& nodeName, const std::string& outputPortName) {
    return nodeName + "\n" + outputPortName;
}

void PortRouter::DispatchToSubscriber(const OutputSubscriber& subscriber, const Message& message, bool blocking) {
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
        } else if (m_statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushRejected();
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
            if (m_readyCallback) {
                m_readyCallback(subscriber.dstNodeName);
            }
        } else if (result.status == MessageQueue::PushStatus::Shutdown) {
            if (m_statisticsEnabled && subscriber.stats != nullptr) {
                subscriber.stats->RecordPushRejected();
            }
        } else if (m_statisticsEnabled && subscriber.stats != nullptr) {
            subscriber.stats->RecordPushDropped(1);
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
    } else if (m_statisticsEnabled && subscriber.stats != nullptr) {
        subscriber.stats->RecordPushDropped(1);
    }
}

}} // namespace nexusflow::executor
