cmake_minimum_required(VERSION 3.16)
project(coverwise_docs_cpp_api LANGUAGES CXX)

find_package(coverwise 1.2 CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE coverwise::coverwise)
