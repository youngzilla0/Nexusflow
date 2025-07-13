file(GLOB_RECURSE SRC_FILES "**.cpp")
add_executable(nexusflow_linear-pipeline ${SRC_FILES})
target_link_libraries(nexusflow_linear-pipeline PRIVATE nexusflow)
