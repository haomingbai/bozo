cmake_minimum_required(VERSION 3.12)

project(bozo VERSION 0.0.1)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules")

# note: boost::coroutine should automatically pull in all the dependencies,
#       however, due to changes in boost it doesn't always work until CMake
#       is updated. we therefore look for all the dependencies ourselves.
find_package(Boost 1.74 COMPONENTS coroutine context system thread atomic REQUIRED)
find_package(PostgreSQL REQUIRED)

# Try and find provided resource pool (e.g. from conan), default to vendored version otherwise
find_package(resource_pool 0.1.0 QUIET)
if (NOT resource_pool_FOUND)
    add_subdirectory(contrib)
endif()

option(BOZO_BUILD_TESTS "Enable tests build" OFF)
option(BOZO_COVERAGE "Enable tests coverage" OFF)
option(BOZO_BUILD_EXAMPLES "Enable examples build" OFF)
option(BOZO_USE_TS_EXECUTOR "Use legacy Boost.Asio TS executor as default" OFF)

set(BOZO_RESOURCE_POOL_TARGET elsid::resource_pool)
if (TARGET resource_pool)
    set(BOZO_RESOURCE_POOL_TARGET resource_pool)
endif()

if (NOT BOZO_USE_TS_EXECUTOR AND TARGET ${BOZO_RESOURCE_POOL_TARGET})
    get_target_property(BOZO_RESOURCE_POOL_DEFINITIONS ${BOZO_RESOURCE_POOL_TARGET} INTERFACE_COMPILE_DEFINITIONS)
    if (BOZO_RESOURCE_POOL_DEFINITIONS AND NOT BOZO_RESOURCE_POOL_DEFINITIONS STREQUAL "BOZO_RESOURCE_POOL_DEFINITIONS-NOTFOUND")
        list(FILTER BOZO_RESOURCE_POOL_DEFINITIONS EXCLUDE REGEX "BOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT")
        set_property(TARGET ${BOZO_RESOURCE_POOL_TARGET} PROPERTY INTERFACE_COMPILE_DEFINITIONS "${BOZO_RESOURCE_POOL_DEFINITIONS}")
    endif()
endif()

set(CMAKE_CXX_EXTENSIONS OFF)

add_library(bozo INTERFACE)
add_library(yandex::bozo ALIAS bozo)

target_compile_features(bozo INTERFACE cxx_std_17)

if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang" OR "${CMAKE_CXX_COMPILER_ID}" STREQUAL "AppleClang")
    # disable warnings about a compiler-specific option used for gnu-compatible compilers
    target_compile_options(bozo INTERFACE $<BUILD_INTERFACE:-Wno-gnu-string-literal-operator-template -Wno-gnu-zero-variadic-macro-arguments>)
endif()


target_include_directories(bozo INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(bozo INTERFACE -DBOOST_COROUTINES_NO_DEPRECATION_WARNING)
target_compile_definitions(bozo INTERFACE -DBOOST_HANA_CONFIG_ENABLE_STRING_UDL)
if (BOZO_USE_TS_EXECUTOR)
    target_compile_definitions(bozo INTERFACE -DBOOST_ASIO_USE_TS_EXECUTOR_AS_DEFAULT)
endif()

target_link_libraries(bozo INTERFACE Boost::coroutine)
target_link_libraries(bozo INTERFACE PostgreSQL::PostgreSQL)
target_link_libraries(bozo INTERFACE elsid::resource_pool)

install(
    DIRECTORY   include/bozo
    DESTINATION include
)

install(
    TARGETS     bozo
    EXPORT      bozo-targets
    DESTINATION lib
)

install(
    EXPORT      bozo-targets
    NAMESPACE   yandex::
    DESTINATION lib/cmake/bozo
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/bozo/bozo-config-version.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

export(
    EXPORT bozo-targets
    FILE "${CMAKE_CURRENT_BINARY_DIR}/bozo/bozo-targets.cmake"
    NAMESPACE yandex::
)

configure_file(cmake/bozo-config.cmake
    "${CMAKE_CURRENT_BINARY_DIR}/bozo/bozo-config.cmake"
    COPYONLY
)

install(
    FILES
        cmake/bozo-config.cmake
        cmake/modules/FindPostgreSQL.cmake
        "${CMAKE_CURRENT_BINARY_DIR}/bozo/bozo-config-version.cmake"
    DESTINATION
        lib/cmake/bozo
)

if(BOZO_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(BOZO_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(BOZO_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
