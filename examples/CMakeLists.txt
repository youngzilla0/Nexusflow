file(GLOB_RECURSE SRC_FILES "**.cpp")
add_executable(example ${SRC_FILES})
target_link_libraries(example PRIVATE pipeline)
