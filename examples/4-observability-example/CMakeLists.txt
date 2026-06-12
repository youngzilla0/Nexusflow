file(GLOB_RECURSE SRC_FILES "**.cpp")
add_executable(nexusflow_observability_example ${SRC_FILES})
target_link_libraries(nexusflow_observability_example PRIVATE nexusflow)
