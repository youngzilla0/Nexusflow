get_property(ALL_TEST_SOURCES GLOBAL PROPERTY GLOBAL_TEST_SOURCES)

if(NOT ALL_TEST_SOURCES)
    message(WARNING "No test sources found – will not build unit tests, ALL_TEST_SOURCES: ${ALL_TEST_SOURCES}")
    return()
endif()

add_executable(unit_tests main.cpp ${ALL_TEST_SOURCES})

target_link_libraries(unit_tests PRIVATE pipeline GTest::gtest) # 示例，可以添加GTest

add_test(NAME all_unit_tests COMMAND unit_tests)
