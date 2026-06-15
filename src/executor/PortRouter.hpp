#ifndef NEXUSFLOW_EXECUTOR_PORT_ROUTER_HPP
#define NEXUSFLOW_EXECUTOR_PORT_ROUTER_HPP

#include "statistics/Statistics.hpp"
#include "base/Define.hpp"
#include "common/ViewPtr.hpp"

#include <nexusflow/Message.hpp>
#include <nexusflow/Ports.hpp>
#include <nexusflow/PipelineEvents.hpp>
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
 *
 * 可以把它理解成 Executor 的“消息投递层”。
 */
class PortRouter {
public:
    using PortStatsStatePtr = Statistics::PortStatsStatePtr;
    using ReadyCallback = std::function<void(const std::string&)>;
    using QueueFullPolicyProvider = std::function<QueueFullPolicy()>;
    using MessageEventCallback = std::function<void(const PipelineMessageEvent&)>;

    /**
     * @brief 单个输出订阅边的运行时绑定信息。
     *
     * 一条输出边在路由层上会被拆成一个或多个订阅者，
     * 每个订阅者记录其目标节点、输入端口、消息队列和统计状态。
     */
    struct OutputSubscriber {
        std::string dstNodeName; ///< 目标节点名称。
        std::string dstInputPortName; ///< 目标输入端口名称。
        ViewPtr<MessageQueue> queue; ///< 目标输入队列。
        PortStatsStatePtr stats; ///< 该边的统计状态；统计关闭时可为空。
    };

    /**
     * @brief 构造一个 PortRouter。
     * @param statisticsEnabled 是否启用统计。
     * @param queueFullPolicyProvider 提供当前非阻塞满队列策略。
     * @param readyCallback 下游节点 ready 时的回调。
     * @param messageEventCallback 消息丢弃/拒绝时的回调。
     *
     * `queueFullPolicyProvider` 会在每次非阻塞投递时查询当前队列满策略。
     */
    PortRouter(bool statisticsEnabled,
               QueueFullPolicyProvider queueFullPolicyProvider,
               ReadyCallback readyCallback,
               MessageEventCallback messageEventCallback);

    /**
     * @brief 为节点注册一条输出订阅边。
     *
     * 同一个输出端口可以对应多个订阅者，这里会同时登记到广播表和定向路由表。
     */
    void AddOutputQueue(const std::string& nodeName,
                        const std::string& outputPortName,
                        const std::string& dstNodeName,
                        const std::string& dstInputPortName,
                        ViewPtr<MessageQueue> queue,
                        const PortStatsStatePtr& stats);

    /**
     * @brief 向节点的所有广播订阅者分发消息。
     *
     * 适用于 Broadcast() 语义的输出。
     */
    void Emit(const std::string& nodeName, const Message& message, bool blocking);

    /**
     * @brief 向节点指定输出端口的订阅者分发消息。
     *
     * 适用于按端口名选择的 Route() 语义输出。
     */
    void Route(const std::string& nodeName, const std::string& outputPortName, const Message& message, bool blocking);

    /**
     * @brief 更新消息事件回调。
     * @param messageEventCallback 新的消息事件回调。
     *
     * 新回调会替换旧回调，用于后续投递异常通知。
     */
    void SetMessageEventCallback(MessageEventCallback messageEventCallback);

private:
    static std::string MakeOutputKey(const std::string& nodeName, const std::string& outputPortName);
    void DispatchToSubscriber(const OutputSubscriber& subscriber,
                              const std::string& srcNodeName,
                              const std::string& srcPortName,
                              const Message& message,
                              bool blocking);
    void NotifyMessageEvent(const OutputSubscriber& subscriber,
                            const std::string& srcNodeName,
                            const std::string& srcPortName,
                            PipelineMessageEventType type,
                            std::uint64_t affectedCount,
                            bool blocking,
                            const std::string& reason) const;

private:
    bool m_statisticsEnabled = true;
    QueueFullPolicyProvider m_queueFullPolicyProvider;
    ReadyCallback m_readyCallback;
    MessageEventCallback m_messageEventCallback;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_broadcastSubscribers;
    std::unordered_map<std::string, std::vector<OutputSubscriber>> m_outputSubscribers;
};

}} // namespace nexusflow::executor

#endif // NEXUSFLOW_EXECUTOR_PORT_ROUTER_HPP
