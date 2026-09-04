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

# Extra sources beyond TEST_SOURCE are compiled straight into the executable.
# Shared test support that replaces global symbols -- the allocation counter's
# operator new family -- must be attached this way: a static library would only
# be pulled in on an undefined-symbol reference, which a global replacement is
# not, so the linker could silently drop the instrumentation.
function(coverwise_add_test TEST_NAME TEST_SOURCE)
  add_executable(${TEST_NAME} ${TEST_SOURCE} ${ARGN})
  target_compile_features(${TEST_NAME} PRIVATE cxx_std_17)
  target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
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
coverwise_add_test(combinatorics_alloc_test util/combinatorics_alloc_test.cpp
  support/allocation_counter.cpp)
coverwise_add_test(string_util_test util/string_util_test.cpp)

# Model tests
coverwise_add_test(boundary_test model/boundary_test.cpp)
coverwise_add_test(options_validation_test model/options_validation_test.cpp)
coverwise_add_test(parameter_test model/parameter_test.cpp)
coverwise_add_test(test_case_test model/test_case_test.cpp)

coverwise_add_test(constraint_parser_test model/constraint_parser_test.cpp)
coverwise_add_test(constraint_atom_cost_test model/constraint_atom_cost_test.cpp)

# Algo tests
coverwise_add_test(greedy_test algo/greedy_test.cpp)

# Core tests
coverwise_add_test(constraint_solver_test core/constraint_solver_test.cpp)
coverwise_add_test(coverage_engine_test core/coverage_engine_test.cpp
  support/allocation_counter.cpp)
coverwise_add_test(generator_test core/generator_test.cpp)

# Validator tests
coverwise_add_test(coverage_validator_test validator/coverage_validator_test.cpp
  support/allocation_counter.cpp)
coverwise_add_test(constraint_validator_test validator/constraint_validator_test.cpp)
coverwise_add_test(equivalence_class_test validator/equivalence_class_test.cpp)

# Integration tests
coverwise_add_test(integration_test integration/generate_and_validate_test.cpp)
coverwise_add_test(benchmark_test integration/benchmark_test.cpp)

# Documentation contract test: reads the shipped markdown and the sources it
# describes, so it needs the source tree rather than the build tree.
#
# What the repository ships is what it tracks. Walking the source tree instead
# would take in whatever a working copy happens to hold -- an agent instruction
# file, a local draft -- and put the gate's verdict at the mercy of untracked
# files, so the document list comes from git and is regenerated whenever the
# index changes.
find_package(Git REQUIRED)
execute_process(
  COMMAND "${GIT_EXECUTABLE}" -C "${PROJECT_SOURCE_DIR}" ls-files "*.md"
  OUTPUT_VARIABLE coverwise_tracked_documents
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)
string(REPLACE "\n" ";" coverwise_tracked_documents "${coverwise_tracked_documents}")
set(COVERWISE_SHIPPED_DOCUMENTS "")
foreach(document IN LISTS coverwise_tracked_documents)
  # docs/ and the repository root are where the documents a reader is given
  # live; a README inside a binding is that package's own and is checked there.
  if(document MATCHES "^docs/" OR NOT document MATCHES "/")
    string(APPEND COVERWISE_SHIPPED_DOCUMENTS "\"${document}\",")
  endif()
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/.git/index")

coverwise_add_test(docs_contract_test integration/docs_contract_test.cpp)
target_compile_definitions(docs_contract_test
  PRIVATE COVERWISE_REPO_ROOT="${PROJECT_SOURCE_DIR}"
          COVERWISE_SHIPPED_DOCUMENTS=${COVERWISE_SHIPPED_DOCUMENTS})

# CLI integration test drives the built coverwise binary via popen.
# Requires the CLI target, which only exists for native (non-WASM) builds.
if(COVERWISE_BUILD_CLI AND NOT COVERWISE_BUILD_WASM)
  coverwise_add_test(cli_test integration/cli_test.cpp)
  target_compile_definitions(cli_test
    PRIVATE COVERWISE_CLI_PATH="$<TARGET_FILE:coverwise>")
  add_dependencies(cli_test coverwise)

  coverwise_add_test(negative_coverage_test integration/negative_coverage_test.cpp)
  target_compile_definitions(negative_coverage_test
    PRIVATE COVERWISE_CLI_PATH="$<TARGET_FILE:coverwise>")
  add_dependencies(negative_coverage_test coverwise)

  # Runs the worked examples of the CLI reference, so it needs both the source
  # tree that holds them and the binary they document.
  coverwise_add_test(docs_cli_examples_test integration/docs_cli_examples_test.cpp)
  target_compile_definitions(docs_cli_examples_test
    PRIVATE COVERWISE_CLI_PATH="$<TARGET_FILE:coverwise>"
            COVERWISE_REPO_ROOT="${PROJECT_SOURCE_DIR}")
  add_dependencies(docs_cli_examples_test coverwise)
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

add_test(NAME cmake_add_subdirectory_cli_fixture
  COMMAND ${CMAKE_COMMAND}
    -DMODE=add_subdirectory
    -DCONFIG=$<CONFIG>
    -DCOVERWISE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCOVERWISE_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/add_subdirectory_cli
    -DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake-add-subdirectory-cli
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_fixture.cmake
)

add_test(NAME cmake_find_package_fixture
  COMMAND ${CMAKE_COMMAND}
    -DMODE=find_package
    -DCONFIG=$<CONFIG>
    -DCOVERWISE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCOVERWISE_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DCOVERWISE_EXPECTED_VERSION=${PROJECT_VERSION}
    -DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/find_package
    -DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake-find-package
    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/run_fixture.cmake
)

add_test(NAME cmake_installed_headers_fixture
  COMMAND ${CMAKE_COMMAND}
    -DMODE=find_package
    -DCONFIG=$<CONFIG>
    -DCOVERWISE_SOURCE_DIR=${PROJECT_SOURCE_DIR}
    -DCOVERWISE_BINARY_DIR=${PROJECT_BINARY_DIR}
    -DFIXTURE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/installed_headers
    -DFIXTURE_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}/cmake-installed-headers
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
