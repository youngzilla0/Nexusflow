file(GLOB APP_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_executable(app ${APP_SOURCES})


# target_link_libraries(app PRIVATE pipeline)

# 强制把 pipeline 静态库完整依赖，否则注册工厂的宏不会运行(文件在cpp中)
# TODO: 可能会二进制膨胀问题，后期优化.
target_link_libraries(app
    PRIVATE
    -Wl,--whole-archive pipeline -Wl,--no-whole-archive
)

