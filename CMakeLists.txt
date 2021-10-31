cmake_minimum_required(VERSION 3.15)

project(efvitcp CXX)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -O3 -g -std=c++11)

include_directories(include)
link_libraries(ciul1)

add_subdirectory(test)
