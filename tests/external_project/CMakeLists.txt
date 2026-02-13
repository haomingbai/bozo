cmake_minimum_required(VERSION 3.12)
project(my_bozo_using_project)

find_package(bozo REQUIRED)
add_executable(my_app ../../examples/request.cpp warning_option_propagation.cpp)
target_compile_options(my_app PRIVATE -Wall -Wextra -pedantic -Werror)
target_link_libraries(my_app PRIVATE yandex::bozo)
