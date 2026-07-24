# coverwise tests

include(FetchContent)
# Test dependencies must not become part of coverwise's install component.
set(INSTALL_GTEST OFF)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

function(coverwise_add_test TEST_NAME TEST_SOURCE)
  add_executable(${TEST_NAME} ${TEST_SOURCE})
  target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
  target_link_libraries(${TEST_NAME} PRIVATE coverwise::coverwise GTest::gtest_main)
  coverwise_enable_warnings(${TEST_NAME})
  coverwise_enable_sanitizers(${TEST_NAME})
  # Some generator fixtures initialize enough static state that test discovery
  # can exceed CMake's five-second default on a loaded CI runner. Discovery is
  # still a build-time check; give it a bounded, realistic window instead of
  # intermittently deleting the just-linked test executable.
  gtest_discover_tests(${TEST_NAME} DISCOVERY_TIMEOUT 30)
endfunction()

# Util tests
coverwise_add_test(bitset_test util/bitset_test.cpp)
coverwise_add_test(rng_test util/rng_test.cpp)
coverwise_add_test(combinatorics_test util/combinatorics_test.cpp)
coverwise_add_test(string_util_test util/string_util_test.cpp)

# Model tests
coverwise_add_test(boundary_test model/boundary_test.cpp)
coverwise_add_test(parameter_test model/parameter_test.cpp)
coverwise_add_test(test_case_test model/test_case_test.cpp)

coverwise_add_test(constraint_parser_test model/constraint_parser_test.cpp)

# Algo tests
coverwise_add_test(greedy_test algo/greedy_test.cpp)

# Core tests
coverwise_add_test(coverage_engine_test core/coverage_engine_test.cpp)
coverwise_add_test(generator_test core/generator_test.cpp)

# Validator tests
coverwise_add_test(coverage_validator_test validator/coverage_validator_test.cpp)
coverwise_add_test(constraint_validator_test validator/constraint_validator_test.cpp)
coverwise_add_test(equivalence_class_test validator/equivalence_class_test.cpp)

# Integration tests
coverwise_add_test(integration_test integration/generate_and_validate_test.cpp)
coverwise_add_test(benchmark_test integration/benchmark_test.cpp)

# CLI integration test drives the built coverwise binary via popen.
# Requires the CLI target, which only exists for native (non-WASM) builds.
if(COVERWISE_BUILD_CLI AND NOT COVERWISE_BUILD_WASM)
  coverwise_add_test(cli_test integration/cli_test.cpp)
  target_compile_definitions(cli_test
    PRIVATE COVERWISE_CLI_PATH="$<TARGET_FILE:coverwise>")
  add_dependencies(cli_test coverwise)
endif()

add_test(NAME cmake_add_subdirectory_fixture
  COMMAND ${CMAKE_COMMAND}
    -DMODE=add_subdirectory
    -DCONFIG=$<CONFIG>
    -DCOVERWISE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCOVERWISE_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/add_subdirectory
    -DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake-add-subdirectory
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_fixture.cmake
)

add_test(NAME cmake_find_package_fixture
  COMMAND ${CMAKE_COMMAND}
    -DMODE=find_package
    -DCONFIG=$<CONFIG>
    -DCOVERWISE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCOVERWISE_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/find_package
    -DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake-find-package
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_fixture.cmake
)

add_test(NAME docs_cpp_api_fixture
  COMMAND ${CMAKE_COMMAND}
    -DMODE=find_package
    -DCONFIG=$<CONFIG>
    -DCOVERWISE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCOVERWISE_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/docs_cpp_api
    -DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/docs-cpp-api
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_fixture.cmake
)
