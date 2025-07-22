#ifndef NEXUSFLOW_ACTORCONTEXT_HPP
#define NEXUSFLOW_ACTORCONTEXT_HPP

#include "common/ViewPtr.hpp"
#include "monitoring/Metrics.hpp"
#include "monitoring/Monitoring.hpp"
#include "nexusflow/Config.hpp"
#include <string>
#include <unordered_map>

namespace nexusflow {

struct ActorContext {
    //////////////////////////
    // --- Actor Config ---
    //////////////////////////
    ViewPtr<Config> config;

    //////////////////////////
    // --- Monitoring ---
    //////////////////////////
    // --- Worker Metrics ---
    // Total number of messages successfully pulled from the input queue by the worker.
    monitoring::Counter* workerPulledMessagesTotal = nullptr;
    // Latency of a full worker cycle (pull -> process -> dispatch) in ms.
    monitoring::Summary* workerCycleLatencyMs = nullptr;

    // --- Module Metrics ---
    // Pure processing latency of the Module::Process() function in ms.
    monitoring::Summary* moduleProcessLatencyMs = nullptr;

    // --- Dispatcher Metrics ---
    // Dispatcher Metrics - now dimensional
    // Key: downstream module name, Value: a counter for that specific connection
    std::map<std::string, monitoring::Counter*> dispatcherSentTotalPerDownstream;
    std::map<std::string, monitoring::Counter*> dispatcherDroppedTotalPerDownstream;
};

} // namespace nexusflow

#endif