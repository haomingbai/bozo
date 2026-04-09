include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.zip
)
FetchContent_MakeAvailable(googletest)

function(bozo_add_test target_name source_file label)
  add_executable(${target_name} ${source_file})
  target_link_libraries(${target_name}
    PRIVATE
      bozo::bozo
      GTest::gtest_main
      GTest::gmock
  )
  target_include_directories(${target_name}
    PRIVATE
      ${PROJECT_SOURCE_DIR}/tests/support
  )

  add_test(NAME ${target_name} COMMAND ${target_name})
  set_tests_properties(${target_name} PROPERTIES LABELS ${label})
endfunction()

set(BOZO_INTEGRATION_TEST_EFFECTIVE_CONNINFO "${BOZO_INTEGRATION_TEST_CONNINFO}")
set(BOZO_INTEGRATION_FIXTURE_REQUIRED FALSE)

if(BOZO_BUILD_INTEGRATION_TESTS AND NOT BOZO_INTEGRATION_TEST_CONNINFO)
  set(BOZO_INTEGRATION_FIXTURE_REQUIRED TRUE)
  set(BOZO_INTEGRATION_TEST_EFFECTIVE_CONNINFO
    "host=127.0.0.1 port=${BOZO_INTEGRATION_TEST_HOST_PORT} dbname=bozo_test user=bozo password=bozo"
  )

  add_test(
    NAME bozo_postgres_fixture_setup
    COMMAND ${CMAKE_COMMAND} -E env
      BOZO_CONTAINER_RUNTIME=${BOZO_INTEGRATION_TEST_CONTAINER_RUNTIME}
      BOZO_CONTAINER_IMAGE=${BOZO_INTEGRATION_TEST_CONTAINER_IMAGE}
      BOZO_CONTAINER_NAME=${BOZO_INTEGRATION_TEST_CONTAINER_NAME}
      BOZO_CONTAINER_HOST_PORT=${BOZO_INTEGRATION_TEST_HOST_PORT}
      BOZO_CONTAINER_DB=bozo_test
      BOZO_CONTAINER_USER=bozo
      BOZO_CONTAINER_PASSWORD=bozo
      sh ${PROJECT_SOURCE_DIR}/tests/support/postgres_container.sh start
  )
  set_tests_properties(bozo_postgres_fixture_setup PROPERTIES
    FIXTURES_SETUP bozo-postgres
    LABELS integration
  )

  add_test(
    NAME bozo_postgres_fixture_cleanup
    COMMAND ${CMAKE_COMMAND} -E env
      BOZO_CONTAINER_RUNTIME=${BOZO_INTEGRATION_TEST_CONTAINER_RUNTIME}
      BOZO_CONTAINER_IMAGE=${BOZO_INTEGRATION_TEST_CONTAINER_IMAGE}
      BOZO_CONTAINER_NAME=${BOZO_INTEGRATION_TEST_CONTAINER_NAME}
      BOZO_CONTAINER_HOST_PORT=${BOZO_INTEGRATION_TEST_HOST_PORT}
      BOZO_CONTAINER_DB=bozo_test
      BOZO_CONTAINER_USER=bozo
      BOZO_CONTAINER_PASSWORD=bozo
      sh ${PROJECT_SOURCE_DIR}/tests/support/postgres_container.sh stop
  )
  set_tests_properties(bozo_postgres_fixture_cleanup PROPERTIES
    FIXTURES_CLEANUP bozo-postgres
    LABELS integration
  )
endif()

function(bozo_add_integration_test target_name source_file)
  bozo_add_test(${target_name} ${source_file} integration)
  set_tests_properties(${target_name} PROPERTIES
    ENVIRONMENT "BOZO_PG_TEST_CONNINFO=${BOZO_INTEGRATION_TEST_EFFECTIVE_CONNINFO}"
    TIMEOUT 120
  )
  if(BOZO_INTEGRATION_FIXTURE_REQUIRED)
    set_tests_properties(${target_name} PROPERTIES FIXTURES_REQUIRED bozo-postgres)
  endif()
endfunction()

add_subdirectory(general)
