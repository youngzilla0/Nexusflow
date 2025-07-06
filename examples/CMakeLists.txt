file(GLOB_RECURSE SRC_FILES "**.cpp")

add_executable(example ${SRC_FILES})

# target_link_libraries(example PRIVATE pipeline)
# 强制把 pipeline 静态库完整依赖，否则注册工厂的宏不会运行(文件在cpp中)
# TODO: 可能会二进制膨胀问题，后期优化.
target_link_libraries(example
    PRIVATE
    -Wl,--whole-archive pipeline -Wl,--no-whole-archive
)

