#ifndef NEXUSFLOW_PIPELINE_STATISTICS_HPP
#define NEXUSFLOW_PIPELINE_STATISTICS_HPP

#include <nexusflow/StatisticsTypes.hpp>

#include <string>
#include <vector>

namespace nexusflow {

class Pipeline;

/**
 * @brief Pipeline 运行时统计的聚合快照。
 *
 * 该结构表示某一时刻从 Pipeline 采集到的节点级与边级统计数据，
 * 适合用于监控面板、日志打印和调试排障。
 */
struct PipelineStatisticsSnapshot {
    std::vector<NodeStats> nodes;
    std::vector<PortStats> ports;
};

/**
 * @brief Pipeline 运行时统计采集器。
 *
 * 该类型只负责从 Pipeline 拉取统计快照并生成人类可读摘要，
 * 不承担异步事件观察职责。
 */
class PipelineStatisticsCollector {
public:
    /**
     * @brief 绑定一个 Pipeline 作为统计采集源。
     * @param pipeline 目标 Pipeline。
     */
    explicit PipelineStatisticsCollector(const Pipeline& pipeline);

    /**
     * @brief 采集当前时刻的统计快照。
     * @return 当前节点级与边级统计的聚合结果。
     */
    PipelineStatisticsSnapshot Snapshot() const;

    /**
     * @brief 生成人类可读的统计摘要字符串。
     * @return 适合日志或控制台输出的摘要文本。
     */
    std::string Describe() const;

private:
    const Pipeline& m_pipeline;
};

} // namespace nexusflow

#endif // NEXUSFLOW_PIPELINE_STATISTICS_HPP
