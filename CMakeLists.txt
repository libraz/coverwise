cmake_minimum_required(VERSION 3.16)

# package.json is the release version source of truth for npm, CMake, and tags.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/package.json" COVERWISE_VERSION_LINE
     REGEX "^[ \t]*\"version\"[ \t]*:")
string(REGEX REPLACE ".*\"version\"[ \t]*:[ \t]*\"([0-9]+\\.[0-9]+\\.[0-9]+)\".*" "\\1"
       COVERWISE_VERSION "${COVERWISE_VERSION_LINE}")
if(NOT COVERWISE_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR "Could not read a semantic version from package.json")
endif()
project(coverwise VERSION ${COVERWISE_VERSION} LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(COVERWISE_TOP_LEVEL ON)
else()
  set(COVERWISE_TOP_LEVEL OFF)
endif()

if(COVERWISE_TOP_LEVEL)
  # Keep the original top-level switches compatible with existing build scripts.
  option(BUILD_TESTING "Build coverwise tests" ON)
  option(BUILD_CLI "Build the coverwise CLI tool" ON)
  option(BUILD_WASM "Build coverwise for WebAssembly" OFF)
  option(ENABLE_COVERAGE "Enable coverwise code coverage" OFF)
  option(ENABLE_SANITIZERS "Enable coverwise address/undefined sanitizers" OFF)
  set(COVERWISE_BUILD_TESTING ${BUILD_TESTING})
  set(COVERWISE_BUILD_CLI ${BUILD_CLI})
  set(COVERWISE_BUILD_WASM ${BUILD_WASM})
  set(COVERWISE_ENABLE_COVERAGE ${ENABLE_COVERAGE})
  set(COVERWISE_ENABLE_SANITIZERS ${ENABLE_SANITIZERS})
else()
  # Prefixed, disabled-by-default options avoid changing a parent project's build.
  option(COVERWISE_BUILD_TESTING "Build coverwise tests" OFF)
  option(COVERWISE_BUILD_CLI "Build the coverwise CLI tool" OFF)
  option(COVERWISE_BUILD_WASM "Build coverwise for WebAssembly" OFF)
  option(COVERWISE_ENABLE_COVERAGE "Enable coverwise code coverage" OFF)
  option(COVERWISE_ENABLE_SANITIZERS "Enable coverwise address/undefined sanitizers" OFF)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(COVERWISE_TOP_LEVEL)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
  set(COVERWISE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
else()
  set(COVERWISE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tools")
endif()

function(coverwise_enable_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

function(coverwise_enable_sanitizers target)
  if(COVERWISE_ENABLE_SANITIZERS)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
      target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "STATIC_LIBRARY")
        target_link_options(${target} INTERFACE -fsanitize=address,undefined)
      else()
        target_link_options(${target} PRIVATE -fsanitize=address,undefined)
      endif()
    else()
      message(FATAL_ERROR "coverwise sanitizers require GCC or Clang")
    endif()
  endif()
endfunction()

# Coverage settings
if(COVERWISE_ENABLE_COVERAGE)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(STATUS "Enabling code coverage")
  else()
    message(WARNING "Code coverage is only supported with GCC or Clang")
  endif()
endif()

# Source
add_subdirectory(src)

# CLI
if(COVERWISE_BUILD_CLI AND NOT COVERWISE_BUILD_WASM)
  add_subdirectory(tools)
endif()

# Tests
if(COVERWISE_BUILD_TESTING AND NOT COVERWISE_BUILD_WASM)
  enable_testing()
  add_subdirectory(tests)
endif()

# Coverage target
if(COVERWISE_ENABLE_COVERAGE)
  find_program(LCOV_PATH lcov)
  find_program(GENHTML_PATH genhtml)
  if(LCOV_PATH AND GENHTML_PATH)
    add_custom_target(coverage
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/coverage
      COMMAND ${LCOV_PATH} --directory . --zerocounters
      COMMAND ctest --output-on-failure --parallel
      COMMAND ${LCOV_PATH} --directory . --capture --output-file ${CMAKE_BINARY_DIR}/coverage/coverage.info
      COMMAND ${LCOV_PATH} --extract ${CMAKE_BINARY_DIR}/coverage/coverage.info
        '${PROJECT_SOURCE_DIR}/src/*'
        --output-file ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info
      COMMAND ${GENHTML_PATH} ${CMAKE_BINARY_DIR}/coverage/coverage_filtered.info
        --output-directory ${CMAKE_BINARY_DIR}/coverage/html
      COMMAND ${CMAKE_COMMAND} -E echo "Coverage report: ${CMAKE_BINARY_DIR}/coverage/html/index.html"
      WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
      COMMENT "Generating code coverage report"
    )
  endif()
endif()

if(NOT COVERWISE_BUILD_WASM)
  install(TARGETS coverwise_lib
    EXPORT coverwiseTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/coverwise
  )
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/coverwise"
    FILES_MATCHING PATTERN "*.h"
    PATTERN "ts" EXCLUDE
    PATTERN "wasm" EXCLUDE
  )
  install(EXPORT coverwiseTargets
    FILE coverwiseTargets.cmake
    NAMESPACE coverwise::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/coverwise"
  )
  configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/coverwiseConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/coverwiseConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/coverwise"
  )
  write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/coverwiseConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )
  install(FILES
    "${PROJECT_BINARY_DIR}/coverwiseConfig.cmake"
    "${PROJECT_BINARY_DIR}/coverwiseConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/coverwise"
  )
  install(FILES "${PROJECT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/coverwise"
  )
endif()
