# Collect all benchmark source files
file(GLOB_RECURSE ALL_BENCHMARK_SOURCES "Benchmark*.cpp")
add_executable(nexusflow_benchmarks benchmark_main.cpp ${ALL_BENCHMARK_SOURCES})
target_include_directories(nexusflow_benchmarks PRIVATE ${PROJECT_ROOT}/src)
target_link_libraries(nexusflow_benchmarks PRIVATE nexusflow benchmark::benchmark)
