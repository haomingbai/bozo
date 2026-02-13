find_program(CCACHE_FOUND ccache)

if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif()

add_executable(bozo_benchmark bozo_benchmark.cpp)
target_link_libraries(bozo_benchmark bozo)

find_package(Boost COMPONENTS coroutine context system thread atomic program_options REQUIRED)

include(ExternalProject)
ExternalProject_Add(
    NlohmannJson
    GIT_REPOSITORY "https://github.com/nlohmann/json.git"
    GIT_TAG v3.7.3
    CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR} -DBUILD_TESTING=OFF
    UPDATE_COMMAND ""
    LOG_DOWNLOAD ON
    LOG_CONFIGURE ON
    LOG_BUILD ON
)
include_directories(SYSTEM ${CMAKE_CURRENT_BINARY_DIR}/include)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_benchmark PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific error for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_benchmark PRIVATE -Wno-ignored-optimization-argument)
endif()

add_executable(bozo_benchmark_performance performance.cpp)
add_dependencies(bozo_benchmark_performance NlohmannJson)
target_link_libraries(bozo_benchmark_performance bozo)
target_link_libraries(bozo_benchmark_performance Boost::program_options)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_benchmark_performance PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific error for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_benchmark_performance PRIVATE -Wno-ignored-optimization-argument)
endif()
