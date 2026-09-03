cmake_minimum_required(VERSION 3.16)
project(coverwise_find_package_consumer LANGUAGES CXX)

if(NOT DEFINED COVERWISE_EXPECTED_VERSION OR COVERWISE_EXPECTED_VERSION STREQUAL "")
  message(FATAL_ERROR "COVERWISE_EXPECTED_VERSION is required")
endif()

# The installed version file must reject a request from another major version.
find_package(coverwise 99.0 CONFIG QUIET)
if(coverwise_FOUND)
  message(FATAL_ERROR "the installed package satisfied a request outside its major version")
endif()
unset(coverwise_FOUND)

# ... and must publish exactly the version the top-level build derives.
find_package(coverwise ${COVERWISE_EXPECTED_VERSION} EXACT CONFIG REQUIRED)
if(NOT coverwise_VERSION VERSION_EQUAL COVERWISE_EXPECTED_VERSION)
  message(FATAL_ERROR
    "installed coverwise is ${coverwise_VERSION}, expected ${COVERWISE_EXPECTED_VERSION}")
endif()

# The documented C++17 requirement has to travel on the exported target itself,
# not on whatever standard the surrounding toolchain happens to default to.
get_target_property(coverwise_features coverwise::coverwise INTERFACE_COMPILE_FEATURES)
if(NOT "cxx_std_17" IN_LIST coverwise_features)
  message(FATAL_ERROR "coverwise::coverwise does not carry the cxx_std_17 usage requirement")
endif()

# Deliberately below the exported requirement: the consumer only compiles
# because linking coverwise raises its standard.
set(CMAKE_CXX_STANDARD 11)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE coverwise::coverwise)
