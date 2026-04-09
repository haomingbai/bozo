add_executable(bozo_transaction_flow transaction_flow.cc)
target_link_libraries(bozo_transaction_flow PRIVATE bozo::bozo)
set_target_properties(bozo_transaction_flow PROPERTIES
  OUTPUT_NAME transaction-flow
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin/examples"
)
