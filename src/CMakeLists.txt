file(GLOB_RECURSE FILES "**.cpp")

message(STATUS "FILES: ${FILES}")

add_executable(main ${FILES})

install(TARGETS main DESTINATION bin)
