cmake_minimum_required(VERSION 3.10)

get_property(ALL_TEST_SOURCES GLOBAL PROPERTY GLOBAL_TEST_SOURCES)

if(NOT ALL_TEST_SOURCES)
    message(WARNING "No test sources found – will not build unit tests, ALL_TEST_SOURCES: ${ALL_TEST_SOURCES}")
    return()
endif()

file(GLOB_RECURSE CUR_TEST_SOURCES "Test*.cpp")

add_executable(nexusflow_tests gtest_main.cpp ${CUR_TEST_SOURCES} ${ALL_TEST_SOURCES})
target_include_directories(nexusflow_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(nexusflow_tests PRIVATE nexusflow GTest::gtest)

add_test(NAME nexusflow_tests COMMAND nexusflow_tests)