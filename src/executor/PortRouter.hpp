#ifndef NEXUSFLOW_EXECUTOR_PORT_ROUTER_HPP
#define NEXUSFLOW_EXECUTOR_PORT_ROUTER_HPP

#include "Statistics.hpp"
#include "base/Define.hpp"
#include "common/ViewPtr.hpp"

#include <nexusflow/Message.hpp>
#include <nexusflow/PipelineContext.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexusflow { namespace executor {

/**
 * @brief Executor 内部的端口路由与消息分发组件。
 *
 * PortRouter 只关心：
 * - 输出端口到下游订阅者的映射
 * - 广播/路由消息的实际投递
 * - 边级统计更新
 * - 下游节点 ready 回调
 *
 * 它不负责调度策略、join 状态和节点执行循环。
 */
class PortRouter {
public:
    using PortStatsStatePtr = Statistics::PortStatsStatePtr;
    using ReadyCallback = std::function<void(const std::string&)>;
    using QueueFullPolicyProvider = std::function<QueueFullPolicy()>;

    /**
     * @brief 单个输出订阅边的运行时绑定信息。
     */
    struct OutputSubscriber {
        std::string dstNodeName;
        std::string dstInputPortName;
        ViewPtr<MessageQueue> queue;
        PortStatsStatePtr stats;
    };

    /**
     * @brief 构造一个 PortRouter。
     * @param statisticsEnabled 是否启用统计。
     * @param queueFullPolicyProvider 提供当前非阻塞满队列策略。
     * @param readyCallback 下游节点 ready 时的回调。
     */
    PortRouter(bool statisticsEnabled, QueueFullPolicyProvider queueFullPolicyProvider, ReadyCallback readyCallback);

    /**
     * @brief 为节点注册一条输出订阅边。
     */
    void AddOutputQueue(const std::string& nodeName,
                        const std::string& outputPortName,
                        const std::string& dstNodeName,
                        const std::string& dstInputPortName,
                        ViewPtr<MessageQueue> queue,
                        const PortStatsStatePtr& stats);

    /**
     * @brief 向节点的所有广播订阅者分发消息。
     */
    void Emit(const std::string& nodeName, const Message& message, bool blocking);

    /**
     * @brief 向节点指定输出端口的订阅者分发消息。
     */
    void Route(const std::string& nodeName, const std::string& outputPortName, const Message& message, bool blocking);

private:
    static std::string MakeOutputKey(const std::string& nodeName, const std::string& outputPortName);
    void DispatchToSubscriber(const OutputSubscriber& subscriber, const Message& message, bool blocking);

private:
    bool m_statisticsEnabled = true;
    QueueFullPolicyProvider m_queueFullPolicyProvider;
    ReadyCallback m_readyCallback;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_broadcastSubscribers;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_outputSubscribers;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_PORT_ROUTER_HPP
