cmake_minimum_required(VERSION 3.16)
project(coverwise_add_subdirectory_consumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(parent_runtime_output "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")

add_subdirectory("${COVERWISE_SOURCE_DIR}" coverwise)

if(NOT CMAKE_CXX_STANDARD EQUAL 20)
  message(FATAL_ERROR "coverwise changed the consumer C++ standard")
endif()
if(NOT "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}" STREQUAL "${parent_runtime_output}")
  message(FATAL_ERROR "coverwise changed the consumer runtime output directory")
endif()
if(TARGET coverwise)
  message(FATAL_ERROR "coverwise CLI should be disabled by default as a subproject")
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE coverwise::coverwise)
