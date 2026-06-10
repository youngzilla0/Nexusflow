#ifndef NEXUSFLOW_STATISTICS_OPTIONS_HPP
#define NEXUSFLOW_STATISTICS_OPTIONS_HPP

namespace nexusflow {

struct StatisticsOptions {
    bool enableStatistics = true;
    bool enableThroughput = true;
    bool enableLatency = true;

    static StatisticsOptions Default() { return StatisticsOptions{}; }
};

} // namespace nexusflow

#endif // NEXUSFLOW_STATISTICS_OPTIONS_HPP
