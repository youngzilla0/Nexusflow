#include "utils/logging.hpp"
#include <benchmark/benchmark.h>

// --- Main function to run the benchmarks ---

int main(int argc, char** argv) {
    auto logParam = utils::logger::LoggerParam();
    logParam.logLevel = utils::logger::LogLevel::ERR;
    utils::logger::InitializeGlobalLogger(logParam);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) {
        return 1;
    }
    benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return 0;
}