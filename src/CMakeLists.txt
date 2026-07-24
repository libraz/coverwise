# coverwise library

set(COVERWISE_SOURCES
  # model
  model/parameter.cpp
  model/options_validation.cpp
  model/test_case.cpp
  model/constraint_ast.cpp
  model/constraint_parser.cpp

  # util
  util/bitset.cpp
  util/string_util.cpp
  model/boundary.cpp
  util/combinatorics.cpp
  util/rng.cpp

  # algo
  algo/greedy.cpp

  # core
  core/constraint_solver.cpp
  core/coverage_engine.cpp
  core/generator.cpp

  # validator
  validator/coverage_validator.cpp
  validator/constraint_validator.cpp
)

add_library(coverwise_lib STATIC ${COVERWISE_SOURCES})
add_library(coverwise::coverwise ALIAS coverwise_lib)
set_target_properties(coverwise_lib PROPERTIES EXPORT_NAME coverwise)
target_compile_features(coverwise_lib PUBLIC cxx_std_17)
target_include_directories(coverwise_lib PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/coverwise>
)
coverwise_enable_warnings(coverwise_lib)
coverwise_enable_sanitizers(coverwise_lib)

if(COVERWISE_ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(coverwise_lib PRIVATE --coverage -O0 -g)
  target_link_options(coverwise_lib INTERFACE --coverage)
endif()

if(COVERWISE_BUILD_WASM)
  add_subdirectory(binding/wasm)
endif()
