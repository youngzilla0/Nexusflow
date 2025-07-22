#ifndef NEXUSFLOW_ACTORCONTEXT_HPP
#define NEXUSFLOW_ACTORCONTEXT_HPP

#include "common/ViewPtr.hpp"
#include "nexusflow/Config.hpp"
#include "profiling/ProfilerRegistry.hpp"
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
    ViewPtr<profiling::ProfilerRegistry> profilerRegistry;
};

} // namespace nexusflow

#endif