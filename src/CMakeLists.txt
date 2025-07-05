file(GLOB_RECURSE PIPELINE_ALL_FILES "**.cpp")

# Split source files and test files. 
separate_files_by_suffix("Test.cpp" "${PIPELINE_ALL_FILES}" PIPELINE_SRC_FILES PIPELINE_TEST_FILES)
message(STATUS "PIPELINE_ALL_FILES : ${PIPELINE_ALL_FILES}")
message(STATUS "PIPELINE_SRC_FILES : ${PIPELINE_SRC_FILES}")
message(STATUS "PIPELINE_TEST_FILES : ${PIPELINE_TEST_FILES}")

if(PIPELINE_TEST_FILES)
    set_property(GLOBAL APPEND PROPERTY GLOBAL_TEST_SOURCES ${PIPELINE_TEST_FILES})
endif()


set(GLOBAL_TEST_SOURCES ${GLOBAL_TEST_SOURCES} ${SRC_TEST_FILES})

add_library(pipeline STATIC ${PIPELINE_SRC_FILES})
target_include_directories(pipeline PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline
)
target_link_libraries(pipeline PUBLIC
    spdlog::spdlog
)



