bozo_add_test(unit_postgresql_task_test unit_postgresql_task_test.cc unit)

if(BOZO_BUILD_INTEGRATION_TESTS)
  bozo_add_integration_test(integration_postgresql_task_test
    integration_postgresql_task_test.cc
  )
endif()
