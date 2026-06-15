#include "../src/utils/logging.hpp" // TODO: remove

#include <nexusflow/Nexusflow.hpp>

#include <chrono>
#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace nexusflow;
using namespace std::chrono_literals;

namespace {

static constexpr std::size_t kDefaultPayloadBytes = 1024 * 1024;

struct SamplePayload {
    int sequence = 0;
    std::vector<char> bytes;
};

Message MakePayloadMessage(int value,
                           const std::string& sourceName,
                           std::uint64_t messageId,
                           std::size_t payloadBytes = kDefaultPayloadBytes) {
    SamplePayload payload;
    payload.sequence = value;
    payload.bytes.resize(payloadBytes, static_cast<char>('A' + (value % 26)));

    Message message(std::move(payload));
    message.MetaData().sourceName = sourceName;
    message.MetaData().messageId = messageId;
    message.MetaData().timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    return message;
}

class ManualSourceModule : public Module {
public:
    explicit ManualSourceModule(const std::string& name) : Module(name) {
        SetSourcePolicy(SourcePolicy::Manual);
    }

    void Process(const PortInputsView&, PortOutputs&) override {}

    void SendValue(int value, bool blocking) {
        const auto messageId = ++m_nextMessageId;
        Broadcast(MakePayloadMessage(value, GetModuleName(), messageId), blocking);
    }

private:
    std::uint64_t m_nextMessageId = 0;
};

class SlowPassThroughModule : public Module {
public:
    explicit SlowPassThroughModule(const std::string& name) : Module(name) {
        SetTriggerPolicy(TriggerPolicy::OnAnyInput);
    }

    void Process(const PortInputsView& inputs, PortOutputs& outputs) override {
        std::this_thread::sleep_for(40ms);
        const auto* inputMessage = inputs.OnlyMessage();
        if (inputMessage == nullptr) {
            return;
        }

        outputs.Emit(*inputMessage, true);
    }
};

class CollectSinkModule : public Module {
public:
    explicit CollectSinkModule(const std::string& name) : Module(name) {
        SetTriggerPolicy(TriggerPolicy::OnAnyInput);
    }

    void Process(const PortInputsView& inputs, PortOutputs&) override {
        const auto* message = inputs.OnlyMessage();
        if (message == nullptr) {
            return;
        }
        if (const auto* payload = message->BorrowPtr<SamplePayload>()) {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_values.push_back(payload->sequence);
            }
            m_cond.notify_all();
        }
    }

    bool WaitForCount(std::size_t expectedCount, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(m_mutex);
        return m_cond.wait_for(lock, timeout, [this, expectedCount]() { return m_values.size() >= expectedCount; });
    }

    std::vector<int> Values() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_values;
    }

private:
    mutable std::mutex m_mutex;
    std::condition_variable m_cond;
    std::vector<int> m_values;
};

void PrintOverview(const PipelineSummaryStats& summary) {
    std::cout << "\n================ Overview ================\n";
    std::cout << "Attempts:   " << summary.totalPushAttempts << "\n";
    std::cout << "Enqueued:   " << summary.totalEnqueueCount << "\n";
    std::cout << "Dropped:    " << summary.totalDropCount << "\n";
    std::cout << "Rejected:   " << summary.totalRejectCount << "\n";
    std::cout << "Drop Rate:  " << static_cast<int>(summary.dropRate * 100.0 + 0.5) << "%\n";
    std::cout << "Reject Rate: " << static_cast<int>(summary.rejectRate * 100.0 + 0.5) << "%\n";
    std::cout << "Sink Count: " << summary.sinkReceiveCount << "\n";
    std::cout << "Latency Samples: " << summary.latencySampleCount << "\n";
    std::cout << "Latency P50: " << summary.latencyP50Ms << " ms\n";
    std::cout << "Latency P99: " << summary.latencyP99Ms << " ms\n";
    std::cout << "Latency Max: " << summary.latencyMaxMs << " ms\n";
}

void PrintSnapshot(const PipelineObservation& observation) {
    std::cout << "\n[Node Stats]\n";
    for (const auto& node : observation.nodes) {
        std::cout << "  - " << node.nodeName << "\n";
        std::cout << "      Processed: " << node.processCount << "\n";
        std::cout << "      Inputs:    " << node.inputMessageCount << "\n";
        std::cout << "      Dequeued:  " << node.incomingDequeueCount << "\n";
        std::cout << "      Enqueued:  " << node.outgoingEnqueueCount << "\n";
        std::cout << "      Dropped:   " << node.outgoingDropCount << "\n";
        std::cout << "      Rejected:  " << node.outgoingRejectCount << "\n";
        if (node.pendingJoinGroupCount > 0 || node.joinTimeoutDropCount > 0 || node.joinOverflowDropCount > 0) {
            std::cout << "      Pending Joins:  " << node.pendingJoinGroupCount << "\n";
            std::cout << "      Timeout Drops:  " << node.joinTimeoutDropCount << "\n";
            std::cout << "      Overflow Drops: " << node.joinOverflowDropCount << "\n";
        }
    }

    std::cout << "\n[Port Stats]\n";
    for (const auto& port : observation.ports) {
        std::cout << "  - " << port.srcModuleName << ":" << port.srcPortName
                  << " -> " << port.dstModuleName << ":" << port.dstPortName << "\n";
        std::cout << "      Enqueued:   " << port.enqueueCount << "\n";
        std::cout << "      Dropped:    " << port.dropCount << "\n";
        std::cout << "      Rejected:   " << port.rejectCount << "\n";
        std::cout << "      Dequeued:   " << port.dequeueCount << "\n";
        std::cout << "      Depth:      " << port.currentDepth << "\n";
        std::cout << "      Peak Depth: " << port.peakDepth << "\n";
    }
}

} // namespace

int main() {
    utils::logger::LoggerParam params;
    params.logLevel = utils::logger::LogLevel::INFO;
    utils::logger::InitializeGlobalLogger(params);

    try {
        PipelineConfig config;
        config.executorThreadCount = 2;
        config.queueSize = 2;
        config.idleWaitUs = 10;
        config.nonBlockingQueueFullPolicy = QueueFullPolicy::DropTail;
        config.statistics.enableStatistics = true;

        auto source = std::make_shared<ManualSourceModule>("Source");
        auto slowPass = std::make_shared<SlowPassThroughModule>("SlowPass");
        auto sink = std::make_shared<CollectSinkModule>("Sink");

        auto pipeline = PipelineBuilder()
                            .WithName("ObservabilityExample")
                            .AddModule(source)
                            .AddModule(slowPass)
                            .AddModule(sink)
                            .Connect("Source", "SlowPass")
                            .Connect("SlowPass", "Sink")
                            .WithConfig(config)
                            .Build();

        if (pipeline == nullptr) {
            throw std::runtime_error("Failed to build observability example pipeline.");
        }

        auto eventObserver = std::make_shared<CallbackPipelineObserver>();
        eventObserver->OnMessage([](const PipelineMessageEvent& event) {
            std::cout << "[Event] " << (event.type == PipelineMessageEventType::Dropped ? "Message Dropped" : "Message Rejected") << "\n";
            std::cout << "  Path: " << event.srcNodeName << ":" << event.srcPortName
                      << " -> " << event.dstNodeName << ":" << event.dstInputPortName << "\n";
            std::cout << "  Count: " << event.affectedCount << "\n";
            std::cout << "  Reason: " << event.reason << "\n";
        });
        pipeline->AddObserver(eventObserver);

        if (pipeline->Init() != ErrorCode::SUCCESS) {
            throw std::runtime_error("Pipeline initialization failed.");
        }

        if (pipeline->Start() != ErrorCode::SUCCESS) {
            throw std::runtime_error("Pipeline start failed.");
        }

        for (int value = 1; value <= 8; ++value) {
            source->SendValue(value, false);
        }

        sink->WaitForCount(2, 2s);
        std::this_thread::sleep_for(50ms);
        pipeline->Stop();

        PipelineStatisticsCollector statisticsCollector(*pipeline);
        const auto observation = statisticsCollector.Snapshot();
        const auto sinkValues = sink->Values();
        PrintOverview(observation.summary);

        PrintSnapshot(observation);

        std::cout << "\n[Sink Values]\n  ";
        for (const auto value : sinkValues) {
            std::cout << value << " ";
        }
        std::cout << "\n";

        pipeline->DeInit();
    } catch (const std::exception& e) {
        LOG_ERROR("Observability example failed: {}", e.what());
        return 1;
    }

    return 0;
}
