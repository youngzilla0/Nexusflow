#include "../src/utils/logging.hpp" // TODO: remove

#include <nexusflow/Nexusflow.hpp>

#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace nexusflow;
using namespace std::chrono_literals;

namespace {

Message MakeIntMessage(int value, const std::string& sourceName, std::uint64_t messageId) {
    Message message(value);
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
        Broadcast(MakeIntMessage(value, GetModuleName(), messageId), blocking);
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
        if (const auto* value = inputs.OnlyAs<int>()) {
            m_values.push_back(*value);
        }
    }

    const std::vector<int>& Values() const { return m_values; }

private:
    std::vector<int> m_values;
};

void PrintSnapshot(const PipelineObservation& observation) {
    std::cout << "\n[Node Stats]\n";
    for (const auto& node : observation.nodes) {
        std::cout << "  " << node.nodeName
                  << " process=" << node.processCount
                  << " input=" << node.inputMessageCount
                  << " incomingDequeue=" << node.incomingDequeueCount
                  << " outgoingEnqueue=" << node.outgoingEnqueueCount
                  << " outgoingDrop=" << node.outgoingDropCount
                  << " pendingJoins=" << node.pendingJoinGroupCount << "\n";
    }

    std::cout << "\n[Port Stats]\n";
    for (const auto& port : observation.ports) {
        std::cout << "  " << port.srcModuleName << ":" << port.srcPortName
                  << " -> " << port.dstModuleName << ":" << port.dstPortName
                  << " enqueue=" << port.enqueueCount
                  << " drop=" << port.dropCount
                  << " reject=" << port.rejectCount
                  << " dequeue=" << port.dequeueCount
                  << " depth=" << port.currentDepth
                  << " peak=" << port.peakDepth << "\n";
    }
}

} // namespace

int main() {
    utils::logger::LoggerParam params;
    params.logLevel = utils::logger::LogLevel::INFO;
    utils::logger::InitializeGlobalLogger(params);

    try {
        PipelineConfig config;
        config.executorThreadCount = 1;
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

        if (pipeline->Init() != ErrorCode::SUCCESS) {
            throw std::runtime_error("Pipeline initialization failed.");
        }

        if (pipeline->Start() != ErrorCode::SUCCESS) {
            throw std::runtime_error("Pipeline start failed.");
        }

        for (int value = 1; value <= 8; ++value) {
            source->SendValue(value, false);
        }

        std::this_thread::sleep_for(400ms);

        PipelineObserver observer(*pipeline);
        const auto observation = observer.Snapshot();

        std::cout << "PipelineObserver::Describe()\n";
        std::cout << observer.Describe() << "\n";

        PrintSnapshot(observation);

        std::cout << "\n[Sink Values]\n  ";
        for (const auto value : sink->Values()) {
            std::cout << value << " ";
        }
        std::cout << "\n";

        pipeline->Stop();
        pipeline->DeInit();
    } catch (const std::exception& e) {
        LOG_ERROR("Observability example failed: {}", e.what());
        return 1;
    }

    return 0;
}
