#include <benchmark/benchmark.h>

#include <algorithm>
#include <nexusflow/Message.hpp>
#include <nexusflow/Module.hpp>
#include <nexusflow/Pipeline.hpp>
#include <nexusflow/PipelineBuilder.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>

using namespace nexusflow;
using namespace std::chrono;

namespace {

inline std::uint64_t GetNowNs() { return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count(); }

class SinkModule : public Module {
public:
    explicit SinkModule(std::string name) : Module(std::move(name)) {}

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        (void)outputs;

        const auto now = GetNowNs();
        if (auto* startTime = inputs.OnlyAs<std::uint64_t>()) {
            m_totalLatencyNs += (now - *startTime);
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
    std::atomic<std::uint64_t> m_messageCount{0};
    std::atomic<std::uint64_t> m_totalLatencyNs{0};
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
    bool m_blocking;
    int m_simulateLatencyUs = 0;
};

class SourceModule : public Module {
public:
    SourceModule(std::string name, bool blocking = true) : Module(std::move(name)), m_blocking(blocking) {}

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
        : Module(std::move(name)), m_payload(std::make_shared<std::vector<char>>(payloadSize, 'x')), m_blocking(blocking) {}

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

struct PortStatsSummary {
    std::uint64_t enqueueCount = 0;
    std::uint64_t dropCount = 0;
    std::uint64_t rejectCount = 0;
    std::uint64_t dequeueCount = 0;
    std::uint64_t maxPeakDepth = 0;
};

std::uint64_t SumCurrentDepth(const Pipeline& pipeline) {
    std::uint64_t currentDepth = 0;
    for (const auto& stats : pipeline.GetPortStats()) {
        currentDepth += stats.currentDepth;
    }
    return currentDepth;
}

PortStatsSummary SummarizePortStats(const Pipeline& pipeline) {
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

PortStatsSummary DiffPortStats(const PortStatsSummary& before, const PortStatsSummary& after) {
    PortStatsSummary diff;
    diff.enqueueCount = after.enqueueCount - before.enqueueCount;
    diff.dropCount = after.dropCount - before.dropCount;
    diff.rejectCount = after.rejectCount - before.rejectCount;
    diff.dequeueCount = after.dequeueCount - before.dequeueCount;
    diff.maxPeakDepth = after.maxPeakDepth;
    return diff;
}

bool WaitForPipelineDrain(const Pipeline& pipeline, std::chrono::milliseconds timeout) {
    const auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
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
    const auto deadline = steady_clock::now() + timeout;
    while (steady_clock::now() < deadline) {
        if (counterFn() >= expectedCount) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
    return counterFn() >= expectedCount;
}

void PublishPipelineCounters(benchmark::State& state, std::uint64_t sourceSent, std::uint64_t sinkReceived,
                             std::uint64_t totalLatencyNs, std::uint64_t elapsedNs, const PortStatsSummary& portStats, bool delivered,
                             bool drained) {
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

void PublishReportCounters(benchmark::State& state, std::uint64_t sourceSent, std::uint64_t sinkReceived, std::uint64_t totalLatencyNs,
                           std::uint64_t elapsedNs, const PortStatsSummary& portStats, bool delivered, bool drained) {
    PublishPipelineCounters(state, sourceSent, sinkReceived, totalLatencyNs, elapsedNs, portStats, delivered, drained);
    state.counters["ElapsedUsTotal"] = elapsedNs / 1000.0;
    const double avgElapsedUsPerMessage =
        sinkReceived == 0 ? 0.0 : (static_cast<double>(elapsedNs) / 1000.0) / static_cast<double>(sinkReceived);
    state.counters["ElapsedUsAvg"] = avgElapsedUsPerMessage;
}

std::unique_ptr<Pipeline> BuildReportLinearPipeline(const std::shared_ptr<SourceModule>& source,
                                                    const std::shared_ptr<SinkModule>& sink, int depth, const PipelineConfig& config,
                                                    bool blocking, int simulateLatencyUs = 0) {
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

std::unique_ptr<Pipeline> BuildReportPayloadPipeline(const std::shared_ptr<PayloadSourceModule>& source,
                                                     const std::shared_ptr<CountingSinkModule>& sink, int depth,
                                                     const PipelineConfig& config, bool blocking) {
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

std::unique_ptr<Pipeline> BuildReportDiamondJoinPipeline(const std::shared_ptr<SourceModule>& source,
                                                         const std::shared_ptr<JoinSinkModule>& join, int branches,
                                                         const PipelineConfig& config, bool blocking, int simulateLatencyUs = 0) {
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

} // namespace

static void BM_PipelineDiamond_EndToEndLatency_Blocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 100;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const int testCount = 1000;
        const auto startTime = steady_clock::now();
        for (int i = 0; i < testCount; ++i) {
            source->GenerateOne();
        }

        while (sink->GetMessageCount() < static_cast<std::uint64_t>(testCount * 2)) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        const bool drained = WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), true, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_EndToEndLatency_Blocking)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_Blocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent * 2, 2s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_Blocking)->Unit(benchmark::kMicrosecond);

static void BM_PipelineLinear_Throughput_Blocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Pass1", "Pass2")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent, 2s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineLinear_Throughput_Blocking)->Unit(benchmark::kMicrosecond);

static void BM_PipelineLinear_Throughput_BlockingProcessLatency100us(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true, 100);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true, 100);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Pass1", "Pass2")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineLinear_Throughput_BlockingProcessLatency100us)->Unit(benchmark::kMicrosecond);

static void BM_PipelineSinglePath_Throughput_Blocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Pass1", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent, 2s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineSinglePath_Throughput_Blocking)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_BlockingQueueSize100000(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 100000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent * 2, 2s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_BlockingQueueSize100000)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_BlockingProcessLatency100us(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true, 100);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true, 100);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent * 2, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_BlockingProcessLatency100us)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_BlockingWarm(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", true);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", true);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();

        for (int i = 0; i < 100; ++i) {
            source->GenerateOne();
        }
        while (sink->GetMessageCount() < 200) {
            std::this_thread::sleep_for(std::chrono::microseconds(10));
        }
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const auto sourceSent = source->GetCounter();
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, sourceSent * 2, 2s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 500ms);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, sourceSent, sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_BlockingWarm)->Unit(benchmark::kMicrosecond);

static void BM_PipelineDiamond_Throughput_NonBlocking(benchmark::State& state) {
    auto source = std::make_shared<SourceModule>("Source", false);
    auto pass1 = std::make_shared<PassThroughModule>("Pass1", false);
    auto pass2 = std::make_shared<PassThroughModule>("Pass2", false);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = PipelineBuilder()
                        .AddModule(source)
                        .AddModule(pass1)
                        .AddModule(pass2)
                        .AddModule(sink)
                        .Connect("Source", "Pass1")
                        .Connect("Source", "Pass2")
                        .Connect("Pass1", "Sink")
                        .Connect("Pass2", "Sink")
                        .WithConfig(config)
                        .Build();

    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        while (steady_clock::now() - startTime < std::chrono::seconds(1)) {
            source->GenerateOne();
        }
        const bool drained = WaitForPipelineDrain(*pipeline, 2s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishPipelineCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                                DiffPortStats(portStatsBefore, portStatsAfter), true, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_PipelineDiamond_Throughput_NonBlocking)->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineLinearDepth_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr int kTestCount = 20000;

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildReportLinearPipeline(source, sink, depth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, kTestCount, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 1s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineLinearDepth_Blocking)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Arg(32)->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineLinearDepthPayload1KiB_Blocking(benchmark::State& state) {
    const auto depth = static_cast<int>(state.range(0));
    constexpr int kTestCount = 20000;
    constexpr std::size_t kPayloadSize = 1024;

    auto source = std::make_shared<PayloadSourceModule>("Source", kPayloadSize, true);
    auto sink = std::make_shared<CountingSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildReportPayloadPipeline(source, sink, depth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, kTestCount, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 1s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), 0, elapsedNs,
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
        state.counters["PayloadBytes"] = static_cast<double>(kPayloadSize);
        state.counters["EffectivePayloadMiBps"] = elapsedNs == 0
                                                      ? 0.0
                                                      : (static_cast<double>(sink->GetMessageCount()) * kPayloadSize * 1e9) /
                                                            (static_cast<double>(elapsedNs) * 1024.0 * 1024.0);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineLinearDepthPayload1KiB_Blocking)
    ->Arg(1)
    ->Arg(2)
    ->Arg(4)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineWorkerScaling_Blocking(benchmark::State& state) {
    const auto workers = static_cast<std::size_t>(state.range(0));
    constexpr int kTestCount = 20000;
    constexpr int kDepth = 8;

    auto source = std::make_shared<SourceModule>("Source", true);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = workers;

    auto pipeline = BuildReportLinearPipeline(source, sink, kDepth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, kTestCount, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 1s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineWorkerScaling_Blocking)->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineDiamondBranches_BlockingJoin(benchmark::State& state) {
    const auto branches = static_cast<int>(state.range(0));
    constexpr int kTestCount = 10000;

    auto source = std::make_shared<SourceModule>("Source", true);
    std::vector<std::string> joinPorts;
    joinPorts.reserve(static_cast<std::size_t>(branches));
    for (int i = 0; i < branches; ++i) {
        joinPorts.push_back("b" + std::to_string(i));
    }
    auto join = std::make_shared<JoinSinkModule>("Join", joinPorts);

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 8;
    config.maxPendingJoinGroups = 20000;

    auto pipeline = BuildReportDiamondJoinPipeline(source, join, branches, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        join->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&join]() { return join->GetMessageCount(); }, kTestCount, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 1s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), join->GetMessageCount(), join->GetTotalLatencyNs(), elapsedNs,
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
        state.counters["BranchCount"] = static_cast<double>(branches);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineDiamondBranches_BlockingJoin)->Arg(2)->Arg(4)->Arg(8)->Arg(16)->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelineQueueCapacity_NonBlocking(benchmark::State& state) {
    const auto queueSize = static_cast<std::size_t>(state.range(0));
    constexpr int kTestCount = 50000;
    constexpr int kDepth = 4;

    auto source = std::make_shared<SourceModule>("Source", false);
    auto sink = std::make_shared<SinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = queueSize;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;
    config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;

    auto pipeline = BuildReportLinearPipeline(source, sink, kDepth, config, false, 10);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool drained = WaitForPipelineDrain(*pipeline, 5s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), sink->GetTotalLatencyNs(), elapsedNs,
                              DiffPortStats(portStatsBefore, portStatsAfter), true, drained);
        state.counters["QueueSize"] = static_cast<double>(queueSize);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelineQueueCapacity_NonBlocking)
    ->Arg(8)
    ->Arg(16)
    ->Arg(32)
    ->Arg(64)
    ->Arg(128)
    ->Arg(256)
    ->Unit(benchmark::kMicrosecond);

static void BM_ReportPipelinePayloadSize_Blocking(benchmark::State& state) {
    const auto payloadSize = static_cast<std::size_t>(state.range(0));
    constexpr int kTestCount = 20000;
    constexpr int kDepth = 4;

    auto source = std::make_shared<PayloadSourceModule>("Source", payloadSize, true);
    auto sink = std::make_shared<CountingSinkModule>("Sink");

    PipelineConfig config;
    config.queueSize = 10000;
    config.idleWaitUs = 5;
    config.executorThreadCount = 4;

    auto pipeline = BuildReportPayloadPipeline(source, sink, kDepth, config, true);
    pipeline->Init();
    pipeline->Start();

    for (auto _ : state) {
        sink->Reset();
        source->ResetCounter();
        const auto portStatsBefore = SummarizePortStats(*pipeline);

        const auto startTime = steady_clock::now();
        for (int i = 0; i < kTestCount; ++i) {
            source->GenerateOne();
        }
        const bool delivered = WaitForCounterAtLeast([&sink]() { return sink->GetMessageCount(); }, kTestCount, 5s);
        const bool drained = delivered && WaitForPipelineDrain(*pipeline, 1s);
        const auto endTime = steady_clock::now();

        const auto elapsedNs = duration_cast<nanoseconds>(endTime - startTime).count();
        const auto portStatsAfter = SummarizePortStats(*pipeline);

        PublishReportCounters(state, source->GetCounter(), sink->GetMessageCount(), 0, elapsedNs,
                              DiffPortStats(portStatsBefore, portStatsAfter), delivered, drained);
        state.counters["PayloadBytes"] = static_cast<double>(payloadSize);
        state.counters["EffectivePayloadMiBps"] = elapsedNs == 0 ? 0.0
                                                                 : (static_cast<double>(sink->GetMessageCount()) * payloadSize * 1e9) /
                                                                       (static_cast<double>(elapsedNs) * 1024.0 * 1024.0);
    }

    pipeline->Stop();
}
BENCHMARK(BM_ReportPipelinePayloadSize_Blocking)
    ->Arg(64)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Unit(benchmark::kMicrosecond);
