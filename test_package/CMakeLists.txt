cmake_minimum_required(VERSION 3.12)
project(PackageTest CXX)

find_package(bozo REQUIRED)

add_executable(example ../examples/connection_pool.cpp)
target_compile_features(example PRIVATE cxx_std_17)
target_link_libraries(example PRIVATE yandex::bozo)
