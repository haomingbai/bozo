add_executable(bozo_pool_factory pool_factory.cc)
target_link_libraries(bozo_pool_factory PRIVATE bozo::bozo)
set_target_properties(bozo_pool_factory PROPERTIES
  OUTPUT_NAME pool-factory
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/examples"
)
