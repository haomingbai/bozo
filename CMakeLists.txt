cmake_minimum_required(VERSION 3.25)
project(bozo VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0167)
  if(APPLE)
    cmake_policy(SET CMP0167 OLD)
  else()
    cmake_policy(SET CMP0167 NEW)
  endif()
endif()

if(NOT DEFINED BUILD_SHARED_LIBS)
  if(WIN32)
    option(BUILD_SHARED_LIBS "Build shared libraries" OFF)
  else()
    option(BUILD_SHARED_LIBS "Build shared libraries" ON)
  endif()
endif()

if(NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 20)
  set(CMAKE_CXX_STANDARD_REQUIRED ON)
  set(CMAKE_CXX_EXTENSIONS OFF)
endif()

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(BOZO_BUILD_EXAMPLES "Build bozo examples. Default: ON" ON)
option(BOZO_BUILD_TESTS "Build bozo tests. Default: OFF" OFF)
option(BOZO_BUILD_INTEGRATION_TESTS
  "Build bozo integration tests. Default: OFF"
  OFF
)
option(BOZO_FETCH_BSRVCORE
  "Fetch bsrvcore from the network when no local package or source tree is supplied."
  ON
)
option(BOZO_FETCH_OZO
  "Fetch ozo from the network when no local package or source tree is supplied."
  ON
)

set(BOZO_BSRVCORE_CMAKE_DIR "" CACHE PATH
  "Directory containing bsrvcoreConfig.cmake for a local bsrvcore build or install."
)
set(BOZO_BSRVCORE_SOURCE_DIR "" CACHE PATH
  "Path to a local bsrvcore source tree to add as a subdirectory."
)
set(BOZO_BSRVCORE_GIT_REPOSITORY "https://github.com/haomingbai/bsrvcore.git"
  CACHE STRING "Git repository used when bozo fetches bsrvcore."
)
set(BOZO_BSRVCORE_GIT_TAG "v0.16.0"
  CACHE STRING "Git ref used when bozo fetches bsrvcore."
)

set(BOZO_OZO_CMAKE_DIR "" CACHE PATH
  "Directory containing ozo package config files for a local ozo build or install."
)
set(BOZO_OZO_SOURCE_DIR "" CACHE PATH
  "Path to a local ozo source tree to add as a subdirectory."
)
set(BOZO_OZO_GIT_REPOSITORY "https://github.com/haomingbai/ozo.git"
  CACHE STRING "Git repository used when bozo fetches ozo."
)
set(BOZO_OZO_GIT_TAG "b44bdadb1c6bc705a326c26ac6009f1f864c9377"
  CACHE STRING "Git ref used when bozo fetches ozo."
)
set(BOZO_INTEGRATION_TEST_CONNINFO "" CACHE STRING
  "Explicit PostgreSQL conninfo used by bozo integration tests."
)
set(BOZO_INTEGRATION_TEST_CONTAINER_RUNTIME "" CACHE STRING
  "Container runtime used by bozo integration tests. Empty means auto-detect."
)
set(BOZO_INTEGRATION_TEST_CONTAINER_IMAGE "docker.io/library/postgres:16-alpine" CACHE STRING
  "Container image used by bozo integration tests."
)
set(BOZO_INTEGRATION_TEST_CONTAINER_NAME "bozo-postgres-test" CACHE STRING
  "Container name used by bozo integration tests."
)
set(BOZO_INTEGRATION_TEST_HOST_PORT "55432" CACHE STRING
  "Host port bound to PostgreSQL when bozo integration tests use containers."
)

if(BOZO_BUILD_INTEGRATION_TESTS AND NOT BOZO_BUILD_TESTS)
  message(FATAL_ERROR
    "BOZO_BUILD_INTEGRATION_TESTS requires BOZO_BUILD_TESTS=ON."
  )
endif()

set(BOZO_BSRVCORE_RESOLUTION_MODE "preexisting")
set(BOZO_OZO_RESOLUTION_MODE "preexisting")

if(NOT TARGET bsrvcore::bsrvcore)
  if(BOZO_BSRVCORE_CMAKE_DIR)
    find_package(bsrvcore CONFIG QUIET
      PATHS "${BOZO_BSRVCORE_CMAKE_DIR}"
      NO_DEFAULT_PATH
    )
    if(TARGET bsrvcore::bsrvcore)
      set(BOZO_BSRVCORE_RESOLUTION_MODE
          "config:${BOZO_BSRVCORE_CMAKE_DIR}")
    endif()
  endif()

  if(NOT TARGET bsrvcore::bsrvcore)
    find_package(bsrvcore CONFIG QUIET)
    if(TARGET bsrvcore::bsrvcore)
      set(BOZO_BSRVCORE_RESOLUTION_MODE "config:find_package")
    endif()
  endif()

  if(NOT TARGET bsrvcore::bsrvcore AND BOZO_BSRVCORE_SOURCE_DIR)
    if(NOT EXISTS "${BOZO_BSRVCORE_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR
        "BOZO_BSRVCORE_SOURCE_DIR does not point to a bsrvcore source tree: "
        "${BOZO_BSRVCORE_SOURCE_DIR}"
      )
    endif()

    set(BSRVCORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    add_subdirectory(
      "${BOZO_BSRVCORE_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/bsrvcore-build"
    )
    if(TARGET bsrvcore::bsrvcore)
      set(BOZO_BSRVCORE_RESOLUTION_MODE
          "source:${BOZO_BSRVCORE_SOURCE_DIR}")
    endif()
  endif()

  if(NOT TARGET bsrvcore::bsrvcore AND BOZO_FETCH_BSRVCORE)
    include(FetchContent)

    set(BSRVCORE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(BSRVCORE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
      bsrvcore
      GIT_REPOSITORY "${BOZO_BSRVCORE_GIT_REPOSITORY}"
      GIT_TAG "${BOZO_BSRVCORE_GIT_TAG}"
    )
    FetchContent_MakeAvailable(bsrvcore)
    if(TARGET bsrvcore::bsrvcore)
      set(BOZO_BSRVCORE_RESOLUTION_MODE
          "fetch:${BOZO_BSRVCORE_GIT_REPOSITORY}@${BOZO_BSRVCORE_GIT_TAG}")
    endif()
  endif()
endif()

if(NOT TARGET bsrvcore::bsrvcore)
  message(FATAL_ERROR
    "Unable to resolve bsrvcore. Tried local CMake package discovery, "
    "BOZO_BSRVCORE_SOURCE_DIR, and optional FetchContent."
  )
endif()

if(NOT TARGET yandex::ozo)
  if(BOZO_OZO_CMAKE_DIR)
    find_package(ozo CONFIG QUIET
      PATHS "${BOZO_OZO_CMAKE_DIR}"
      NO_DEFAULT_PATH
    )
    if(TARGET yandex::ozo)
      set(BOZO_OZO_RESOLUTION_MODE "config:${BOZO_OZO_CMAKE_DIR}")
    endif()
  endif()

  if(NOT TARGET yandex::ozo)
    find_package(ozo CONFIG QUIET)
    if(TARGET yandex::ozo)
      set(BOZO_OZO_RESOLUTION_MODE "config:find_package")
    endif()
  endif()

  if(NOT TARGET yandex::ozo AND BOZO_OZO_SOURCE_DIR)
    if(NOT EXISTS "${BOZO_OZO_SOURCE_DIR}/CMakeLists.txt")
      message(FATAL_ERROR
        "BOZO_OZO_SOURCE_DIR does not point to an ozo source tree: "
        "${BOZO_OZO_SOURCE_DIR}"
      )
    endif()

    set(OZO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OZO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(OZO_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

    add_subdirectory(
      "${BOZO_OZO_SOURCE_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/_deps/ozo-build"
    )
    if(TARGET yandex::ozo)
      set(BOZO_OZO_RESOLUTION_MODE "source:${BOZO_OZO_SOURCE_DIR}")
    endif()
  endif()

  if(NOT TARGET yandex::ozo AND BOZO_FETCH_OZO)
    include(FetchContent)

    set(OZO_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(OZO_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(OZO_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
      ozo
      GIT_REPOSITORY "${BOZO_OZO_GIT_REPOSITORY}"
      GIT_TAG "${BOZO_OZO_GIT_TAG}"
    )
    FetchContent_MakeAvailable(ozo)
    if(TARGET yandex::ozo)
      set(BOZO_OZO_RESOLUTION_MODE
          "fetch:${BOZO_OZO_GIT_REPOSITORY}@${BOZO_OZO_GIT_TAG}")
    endif()
  endif()
endif()

if(NOT TARGET yandex::ozo)
  message(FATAL_ERROR
    "Unable to resolve ozo. Tried local CMake package discovery, "
    "BOZO_OZO_SOURCE_DIR, and optional FetchContent."
  )
endif()

message(STATUS "bozo resolved bsrvcore via ${BOZO_BSRVCORE_RESOLUTION_MODE}")
message(STATUS "bozo resolved ozo via ${BOZO_OZO_RESOLUTION_MODE}")

add_library(bozo
  ${CMAKE_CURRENT_SOURCE_DIR}/src/postgresql/postgresql_task_state.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/postgresql/postgresql_task_lifecycle.cc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/postgresql/postgresql_task_factory.cc
)
add_library(bozo::bozo ALIAS bozo)

target_compile_features(bozo PUBLIC cxx_std_20)
target_link_libraries(bozo
  PUBLIC
    $<BUILD_INTERFACE:bsrvcore::bsrvcore>
    $<INSTALL_INTERFACE:bsrvcore::bsrvcore>
    $<BUILD_INTERFACE:yandex::ozo>
    $<INSTALL_INTERFACE:yandex::ozo>
)
target_include_directories(bozo
  PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

set_target_properties(bozo PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME bozo
  ARCHIVE_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
  LIBRARY_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/lib"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin"
)

if(NOT WIN32)
  set_target_properties(bozo PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

if(BOZO_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples")
  add_subdirectory(examples)
endif()

if(BOZO_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests")
  enable_testing()
  add_subdirectory(tests)
endif()

set(BOZO_INSTALL_CONFIGDIR "${CMAKE_INSTALL_LIBDIR}/cmake/bozo")

configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/bozoConfig.cmake.in
  ${PROJECT_BINARY_DIR}/bozoConfig.cmake
  INSTALL_DESTINATION ${BOZO_INSTALL_CONFIGDIR}
)

write_basic_package_version_file(
  ${PROJECT_BINARY_DIR}/bozoConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(TARGETS bozo
  EXPORT bozoTargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING PATTERN "*.h*"
)

install(EXPORT bozoTargets
  FILE bozoTargets.cmake
  NAMESPACE bozo::
  DESTINATION ${BOZO_INSTALL_CONFIGDIR}
)

install(FILES
  ${PROJECT_BINARY_DIR}/bozoConfig.cmake
  ${PROJECT_BINARY_DIR}/bozoConfigVersion.cmake
  DESTINATION ${BOZO_INSTALL_CONFIGDIR}
)

if(BOZO_BSRVCORE_RESOLUTION_MODE MATCHES "^config:" AND
   BOZO_OZO_RESOLUTION_MODE MATCHES "^config:")
  export(EXPORT bozoTargets
    FILE "${PROJECT_BINARY_DIR}/bozoTargets.cmake"
    NAMESPACE bozo::
  )
else()
  message(STATUS
    "bozo skipped build-tree export because dependencies came from "
    "${BOZO_BSRVCORE_RESOLUTION_MODE} and ${BOZO_OZO_RESOLUTION_MODE}"
  )
endif()
