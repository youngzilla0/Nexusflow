#ifndef NEXUSFLOW_PIPELINE_BENCHMARK_COMMON_HPP
#define NEXUSFLOW_PIPELINE_BENCHMARK_COMMON_HPP

#include <benchmark/benchmark.h>

#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>
#include <nexusflow/Pipeline.hpp>
#include <nexusflow/PipelineBuilder.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace nexusflow { namespace bench {

inline std::uint64_t GetNowNs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

struct TimedSharedPayload {
    std::uint64_t startNs = 0;
    std::shared_ptr<std::vector<char>> payload;
};

struct PortStatsSummary {
    std::uint64_t enqueueCount = 0;
    std::uint64_t dropCount = 0;
    std::uint64_t rejectCount = 0;
    std::uint64_t dequeueCount = 0;
    std::uint64_t maxPeakDepth = 0;
};

struct LatencySummary {
    double p50Us = 0.0;
    double p90Us = 0.0;
    double p99Us = 0.0;
    double maxUs = 0.0;
};

class SourceModule : public Module {
public:
    explicit SourceModule(std::string name, bool blocking = true) : Module(std::move(name)), m_blocking(blocking) {
        SetSourcePolicy(SourcePolicy::Manual);
    }

    void GenerateOne() {
        Broadcast(Message(GetNowNs()), m_blocking);
        m_counter++;
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }

    void ResetCounter() { m_counter = 0; }

    std::uint64_t GetCounter() const { return m_counter.load(); }

private:
    std::atomic<std::uint64_t> m_counter{0};
    bool m_blocking = true;
};

class PayloadSourceModule : public Module {
public:
    PayloadSourceModule(std::string name, std::size_t payloadSize, bool blocking = true)
        : Module(std::move(name)),
          m_payload(std::make_shared<std::vector<char>>(payloadSize, 'x')),
          m_blocking(blocking) {
        SetSourcePolicy(SourcePolicy::Manual);
    }

    void GenerateOne() {
        Broadcast(Message(m_payload), m_blocking);
        m_counter++;
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }

    void ResetCounter() { m_counter = 0; }

    std::uint64_t GetCounter() const { return m_counter.load(); }

private:
    std::shared_ptr<std::vector<char>> m_payload;
    std::atomic<std::uint64_t> m_counter{0};
    bool m_blocking = true;
};

class TimedPayloadSourceModule : public Module {
public:
    TimedPayloadSourceModule(std::string name, std::size_t payloadSize, bool blocking = true)
        : Module(std::move(name)),
          m_payload(std::make_shared<std::vector<char>>(payloadSize, 'x')),
          m_blocking(blocking) {
        SetSourcePolicy(SourcePolicy::Manual);
    }

    void GenerateOne() {
        TimedSharedPayload payload;
        payload.startNs = GetNowNs();
        payload.payload = m_payload;
        Broadcast(Message(std::move(payload)), m_blocking);
        m_counter++;
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)inputs;
        (void)outputs;
    }

    void ResetCounter() { m_counter = 0; }

    std::uint64_t GetCounter() const { return m_counter.load(); }

private:
    std::shared_ptr<std::vector<char>> m_payload;
    std::atomic<std::uint64_t> m_counter{0};
    bool m_blocking = true;
};

class PassThroughModule : public Module {
public:
    PassThroughModule(std::string name, bool blocking = true, int simulateLatencyUs = 0)
        : Module(std::move(name)), m_blocking(blocking), m_simulateLatencyUs(simulateLatencyUs) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        auto* inputMessage = inputs.OnlyMessage();
        if (inputMessage == nullptr) {
            return;
        }

        if (m_simulateLatencyUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(m_simulateLatencyUs));
        }

        outputs.Emit(*inputMessage, m_blocking);
    }

private:
    bool m_blocking = true;
    int m_simulateLatencyUs = 0;
};

class CountingSinkModule : public Module {
public:
    explicit CountingSinkModule(std::string name) : Module(std::move(name)) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;
        if (inputs.OnlyMessage() != nullptr) {
            m_messageCount++;
        }
    }

    std::uint64_t GetMessageCount() const { return m_messageCount.load(); }

    void Reset() { m_messageCount = 0; }

private:
    std::atomic<std::uint64_t> m_messageCount{0};
};

class LatencySinkModule : public Module {
public:
    explicit LatencySinkModule(std::string name) : Module(std::move(name)) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;

        const auto now = GetNowNs();
        if (auto* startTime = inputs.OnlyAs<std::uint64_t>()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latenciesNs.push_back(now - *startTime);
        }
        m_messageCount++;
    }

    std::uint64_t GetMessageCount() const { return m_messageCount.load(); }

    std::vector<std::uint64_t> LatenciesSnapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_latenciesNs;
    }

    std::uint64_t GetTotalLatencyNs() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::uint64_t total = 0;
        for (const auto latency : m_latenciesNs) {
            total += latency;
        }
        return total;
    }

    void Reset() {
        m_messageCount = 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latenciesNs.clear();
    }

private:
    std::atomic<std::uint64_t> m_messageCount{0};
    mutable std::mutex m_mutex;
    std::vector<std::uint64_t> m_latenciesNs;
};

class TimedPayloadLatencySinkModule : public Module {
public:
    explicit TimedPayloadLatencySinkModule(std::string name) : Module(std::move(name)) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;

        const auto now = GetNowNs();
        if (auto* timedPayload = inputs.OnlyAs<TimedSharedPayload>()) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_latenciesNs.push_back(now - timedPayload->startNs);
        }
        m_messageCount++;
    }

    std::uint64_t GetMessageCount() const { return m_messageCount.load(); }

    std::vector<std::uint64_t> LatenciesSnapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_latenciesNs;
    }

    std::uint64_t GetTotalLatencyNs() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::uint64_t total = 0;
        for (const auto latency : m_latenciesNs) {
            total += latency;
        }
        return total;
    }

    void Reset() {
        m_messageCount = 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latenciesNs.clear();
    }

private:
    std::atomic<std::uint64_t> m_messageCount{0};
    mutable std::mutex m_mutex;
    std::vector<std::uint64_t> m_latenciesNs;
};

class JoinSinkModule : public Module {
public:
    JoinSinkModule(std::string name, std::vector<std::string> inputPorts)
        : Module(std::move(name)), m_inputPorts(std::move(inputPorts)) {
        SetTriggerPolicy(TriggerPolicy::OnAllInputs);
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;

        const auto now = GetNowNs();
        if (!m_inputPorts.empty()) {
            if (auto* startTime = inputs.Get<std::uint64_t>(m_inputPorts.front())) {
                m_totalLatencyNs += (now - *startTime);
            }
        }
        m_messageCount++;
    }

    std::uint64_t GetMessageCount() const { return m_messageCount.load(); }
    std::uint64_t GetTotalLatencyNs() const { return m_totalLatencyNs.load(); }

    void Reset() {
        m_messageCount = 0;
        m_totalLatencyNs = 0;
    }

private:
    std::vector<std::string> m_inputPorts;
    std::atomic<std::uint64_t> m_messageCount{0};
    std::atomic<std::uint64_t> m_totalLatencyNs{0};
};

class JoinLatencySinkModule : public Module {
public:
    JoinLatencySinkModule(std::string name, std::vector<std::string> inputPorts)
        : Module(std::move(name)), m_inputPorts(std::move(inputPorts)) {
        SetTriggerPolicy(TriggerPolicy::OnAllInputs);
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;

        const auto now = GetNowNs();
        if (!m_inputPorts.empty()) {
            if (auto* startTime = inputs.Get<std::uint64_t>(m_inputPorts.front())) {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_latenciesNs.push_back(now - *startTime);
            }
        }
        m_messageCount++;
    }

    std::uint64_t GetMessageCount() const { return m_messageCount.load(); }

    std::vector<std::uint64_t> LatenciesSnapshot() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_latenciesNs;
    }

    std::uint64_t GetTotalLatencyNs() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::uint64_t total = 0;
        for (const auto latency : m_latenciesNs) {
            total += latency;
        }
        return total;
    }

    void Reset() {
        m_messageCount = 0;
        std::lock_guard<std::mutex> lock(m_mutex);
        m_latenciesNs.clear();
    }

private:
    std::vector<std::string> m_inputPorts;
    std::atomic<std::uint64_t> m_messageCount{0};
    mutable std::mutex m_mutex;
    std::vector<std::uint64_t> m_latenciesNs;
};

inline std::uint64_t SumCurrentDepth(const Pipeline& pipeline) {
    std::uint64_t currentDepth = 0;
    for (const auto& stats : pipeline.GetPortStats()) {
        currentDepth += stats.currentDepth;
    }
    return currentDepth;
}

inline PortStatsSummary SummarizePortStats(const Pipeline& pipeline) {
    PortStatsSummary summary;
    for (const auto& stats : pipeline.GetPortStats()) {
        summary.enqueueCount += stats.enqueueCount;
        summary.dropCount += stats.dropCount;
        summary.rejectCount += stats.rejectCount;
        summary.dequeueCount += stats.dequeueCount;
        summary.maxPeakDepth = std::max(summary.maxPeakDepth, stats.peakDepth);
    }
    return summary;
}

inline PortStatsSummary DiffPortStats(const PortStatsSummary& before, const PortStatsSummary& after) {
    PortStatsSummary diff;
    diff.enqueueCount = after.enqueueCount - before.enqueueCount;
    diff.dropCount = after.dropCount - before.dropCount;
    diff.rejectCount = after.rejectCount - before.rejectCount;
    diff.dequeueCount = after.dequeueCount - before.dequeueCount;
    diff.maxPeakDepth = after.maxPeakDepth;
    return diff;
}

inline bool WaitForPipelineDrain(const Pipeline& pipeline, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (SumCurrentDepth(pipeline) == 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
            if (SumCurrentDepth(pipeline) == 0) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return SumCurrentDepth(pipeline) == 0;
}

template <typename CounterFn>
bool WaitForCounterAtLeast(CounterFn&& counterFn, std::uint64_t expectedCount, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (counterFn() >= expectedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return counterFn() >= expectedCount;
}

inline LatencySummary SummarizeLatency(std::vector<std::uint64_t> latenciesNs) {
    LatencySummary summary;
    if (latenciesNs.empty()) {
        return summary;
    }

    std::sort(latenciesNs.begin(), latenciesNs.end());
    auto percentileUs = [&latenciesNs](double percentile) {
        const auto index = static_cast<std::size_t>((percentile / 100.0) * static_cast<double>(latenciesNs.size() - 1));
        return static_cast<double>(latenciesNs[index]) / 1000.0;
    };

    summary.p50Us = percentileUs(50.0);
    summary.p90Us = percentileUs(90.0);
    summary.p99Us = percentileUs(99.0);
    summary.maxUs = static_cast<double>(latenciesNs.back()) / 1000.0;
    return summary;
}

inline void PublishPipelineCounters(benchmark::State& state, std::uint64_t sourceSent, std::uint64_t sinkReceived,
                                    std::uint64_t totalLatencyNs, std::uint64_t elapsedNs, const PortStatsSummary& portStats,
                                    bool delivered, bool drained) {
    const double throughput = elapsedNs == 0 ? 0.0 : (sinkReceived * 1e9) / static_cast<double>(elapsedNs);
    const double avgLatencyNs = sinkReceived == 0 ? 0.0 : static_cast<double>(totalLatencyNs) / sinkReceived;

    state.SetItemsProcessed(static_cast<std::int64_t>(sinkReceived));
    state.counters["Throughput"] = throughput;
    state.counters["SourceSent"] = static_cast<double>(sourceSent);
    state.counters["SinkReceived"] = static_cast<double>(sinkReceived);
    state.counters["PortEnqueued"] = static_cast<double>(portStats.enqueueCount);
    state.counters["PortDropped"] = static_cast<double>(portStats.dropCount);
    state.counters["PortRejected"] = static_cast<double>(portStats.rejectCount);
    state.counters["PortDequeued"] = static_cast<double>(portStats.dequeueCount);
    state.counters["MaxPortPeakDepth"] = static_cast<double>(portStats.maxPeakDepth);
    state.counters["AvgLatencyNs"] = avgLatencyNs;
    state.counters["DeliveryTimedOut"] = delivered ? 0.0 : 1.0;
    state.counters["DrainTimedOut"] = drained ? 0.0 : 1.0;
}

inline void PublishReportCounters(benchmark::State& state, std::uint64_t sourceSent, std::uint64_t sinkReceived,
                                  std::uint64_t totalLatencyNs, std::uint64_t elapsedNs, const PortStatsSummary& portStats,
                                  bool delivered, bool drained) {
    PublishPipelineCounters(state, sourceSent, sinkReceived, totalLatencyNs, elapsedNs, portStats, delivered, drained);
    state.counters["ElapsedUs"] = elapsedNs / 1000.0;
    const double avgElapsedUsPerMessage =
        sinkReceived == 0 ? 0.0 : (static_cast<double>(elapsedNs) / 1000.0) / static_cast<double>(sinkReceived);
    state.counters["AvgElapsedUsPerMessage"] = avgElapsedUsPerMessage;
    state.counters["PerMessageElapsedUs"] = avgElapsedUsPerMessage;
}

inline void PublishLatencyCounters(benchmark::State& state, const std::vector<std::uint64_t>& latenciesNs) {
    const auto latency = SummarizeLatency(latenciesNs);
    state.counters["P50LatencyUs"] = latency.p50Us;
    state.counters["P90LatencyUs"] = latency.p90Us;
    state.counters["P99LatencyUs"] = latency.p99Us;
    state.counters["MaxLatencyUs"] = latency.maxUs;
    state.counters["LatencySamples"] = static_cast<double>(latenciesNs.size());
}

inline std::unique_ptr<Pipeline> BuildLinearPipeline(const std::shared_ptr<SourceModule>& source,
                                                     const std::shared_ptr<LatencySinkModule>& sink,
                                                     int depth,
                                                     const PipelineConfig& config,
                                                     bool blocking,
                                                     int simulateLatencyUs = 0) {
    PipelineBuilder builder;
    builder.AddModule(source);

    std::string previous = source->GetModuleName();
    for (int i = 0; i < depth; ++i) {
        auto pass = std::make_shared<PassThroughModule>("Pass" + std::to_string(i), blocking, simulateLatencyUs);
        builder.AddModule(pass);
        builder.Connect(previous, pass->GetModuleName());
        previous = pass->GetModuleName();
    }

    builder.AddModule(sink);
    builder.Connect(previous, sink->GetModuleName());
    builder.WithConfig(config);
    return builder.Build();
}

inline std::unique_ptr<Pipeline> BuildPayloadPipeline(const std::shared_ptr<PayloadSourceModule>& source,
                                                      const std::shared_ptr<CountingSinkModule>& sink,
                                                      int depth,
                                                      const PipelineConfig& config,
                                                      bool blocking) {
    PipelineBuilder builder;
    builder.AddModule(source);

    std::string previous = source->GetModuleName();
    for (int i = 0; i < depth; ++i) {
        auto pass = std::make_shared<PassThroughModule>("PayloadPass" + std::to_string(i), blocking);
        builder.AddModule(pass);
        builder.Connect(previous, pass->GetModuleName());
        previous = pass->GetModuleName();
    }

    builder.AddModule(sink);
    builder.Connect(previous, sink->GetModuleName());
    builder.WithConfig(config);
    return builder.Build();
}

inline std::unique_ptr<Pipeline> BuildTimedPayloadPipeline(const std::shared_ptr<TimedPayloadSourceModule>& source,
                                                           const std::shared_ptr<TimedPayloadLatencySinkModule>& sink,
                                                           int depth,
                                                           const PipelineConfig& config,
                                                           bool blocking) {
    PipelineBuilder builder;
    builder.AddModule(source);

    std::string previous = source->GetModuleName();
    for (int i = 0; i < depth; ++i) {
        auto pass = std::make_shared<PassThroughModule>("TimedPayloadPass" + std::to_string(i), blocking);
        builder.AddModule(pass);
        builder.Connect(previous, pass->GetModuleName());
        previous = pass->GetModuleName();
    }

    builder.AddModule(sink);
    builder.Connect(previous, sink->GetModuleName());
    builder.WithConfig(config);
    return builder.Build();
}

inline std::unique_ptr<Pipeline> BuildDiamondJoinPipeline(const std::shared_ptr<SourceModule>& source,
                                                          const std::shared_ptr<JoinSinkModule>& join,
                                                          int branches,
                                                          const PipelineConfig& config,
                                                          bool blocking,
                                                          int simulateLatencyUs = 0) {
    PipelineBuilder builder;
    builder.AddModule(source);
    builder.AddModule(join);

    for (int i = 0; i < branches; ++i) {
        auto pass = std::make_shared<PassThroughModule>("Branch" + std::to_string(i), blocking, simulateLatencyUs);
        const auto joinPort = "b" + std::to_string(i);
        builder.AddModule(pass);
        builder.Connect(source->GetModuleName(), pass->GetModuleName());
        builder.Connect(pass->GetModuleName(), kDefaultOutputPort, join->GetModuleName(), joinPort);
    }

    builder.WithConfig(config);
    return builder.Build();
}

inline std::unique_ptr<Pipeline> BuildDiamondJoinLatencyPipeline(const std::shared_ptr<SourceModule>& source,
                                                                 const std::shared_ptr<JoinLatencySinkModule>& join,
                                                                 int branches,
                                                                 const PipelineConfig& config,
                                                                 bool blocking,
                                                                 int simulateLatencyUs = 0) {
    PipelineBuilder builder;
    builder.AddModule(source);
    builder.AddModule(join);

    for (int i = 0; i < branches; ++i) {
        auto pass = std::make_shared<PassThroughModule>("LatencyBranch" + std::to_string(i), blocking, simulateLatencyUs);
        const auto joinPort = "b" + std::to_string(i);
        builder.AddModule(pass);
        builder.Connect(source->GetModuleName(), pass->GetModuleName());
        builder.Connect(pass->GetModuleName(), kDefaultOutputPort, join->GetModuleName(), joinPort);
    }

    builder.WithConfig(config);
    return builder.Build();
}

}} // namespace nexusflow::bench

#endif // NEXUSFLOW_PIPELINE_BENCHMARK_COMMON_HPP
