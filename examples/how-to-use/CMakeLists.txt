file(GLOB_RECURSE SRC_FILES "**.cpp")
add_executable(nexusflow_how-to-use ${SRC_FILES})
target_link_libraries(nexusflow_how-to-use PRIVATE nexusflow)
