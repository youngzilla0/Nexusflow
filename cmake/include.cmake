include("cmake/3rdparty/spdlog.cmake")
include("cmake/3rdparty/yaml-cpp.cmake")

if(ENABLE_TESTING)
    include("cmake/3rdparty/gtest.cmake")
endif()

include("cmake/Summary.cmake")
