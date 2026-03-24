# coverwise library

set(COVERWISE_SOURCES
  # model
  model/parameter.cpp
  model/test_case.cpp
  model/constraint_ast.cpp
  model/constraint_parser.cpp

  # util
  util/bitset.cpp
  model/boundary.cpp
  util/combinatorics.cpp
  util/rng.cpp

  # algo
  algo/greedy.cpp

  # core
  core/coverage_engine.cpp
  core/generator.cpp

  # validator
  validator/coverage_validator.cpp
  validator/constraint_validator.cpp
)

add_library(coverwise_lib STATIC ${COVERWISE_SOURCES})
target_include_directories(coverwise_lib PUBLIC ${CMAKE_SOURCE_DIR}/src)

if(BUILD_WASM)
  add_subdirectory(wasm)
endif()
