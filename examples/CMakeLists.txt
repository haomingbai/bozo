find_program(CCACHE_FOUND ccache)

if(CCACHE_FOUND)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
    set_property(GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif()

add_executable(bozo_request request.cpp)
target_link_libraries(bozo_request bozo)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_request PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific errors for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_request PRIVATE -Wno-ignored-optimization-argument)
endif()

add_executable(bozo_transaction transaction.cpp)
target_link_libraries(bozo_transaction bozo)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_transaction PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific errors for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_transaction PRIVATE -Wno-ignored-optimization-argument)
endif()

add_executable(bozo_retry_request retry_request.cpp)
target_link_libraries(bozo_retry_request bozo)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_retry_request PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific errors for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_retry_request PRIVATE -Wno-ignored-optimization-argument)
endif()

add_executable(bozo_role_based_request role_based_request.cpp)
target_link_libraries(bozo_role_based_request bozo)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_role_based_request PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific errors for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_role_based_request PRIVATE -Wno-ignored-optimization-argument)
endif()

add_executable(bozo_connection_pool connection_pool.cpp)
target_link_libraries(bozo_connection_pool bozo)

# enable a bunch of warnings and make them errors
target_compile_options(bozo_connection_pool PRIVATE -Wall -Wextra -Wsign-compare -pedantic -Werror)

# ignore specific errors for clang
if ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    target_compile_options(bozo_connection_pool PRIVATE -Wno-ignored-optimization-argument)
endif()
