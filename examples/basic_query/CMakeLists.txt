add_executable(bozo_basic_query basic_query.cc)
target_link_libraries(bozo_basic_query PRIVATE bozo::bozo)
set_target_properties(bozo_basic_query PROPERTIES
  OUTPUT_NAME basic-query
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/examples"
)
