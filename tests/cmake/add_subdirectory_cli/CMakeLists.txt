cmake_minimum_required(VERSION 3.16)
project(coverwise_add_subdirectory_cli_consumer LANGUAGES CXX)

# The sibling embedding fixture covers the defaults. This one turns coverwise's
# own switches on, because "an embedded coverwise installs nothing" has to hold
# for every option value the parent can choose, not just the quiet defaults.
set(COVERWISE_BUILD_CLI ON CACHE BOOL "" FORCE)

add_subdirectory("${COVERWISE_SOURCE_DIR}" coverwise)

if(NOT TARGET coverwise)
  message(FATAL_ERROR "the coverwise CLI was requested but no target was created")
endif()

add_executable(consumer ../add_subdirectory/main.cpp)
target_link_libraries(consumer PRIVATE coverwise::coverwise)
