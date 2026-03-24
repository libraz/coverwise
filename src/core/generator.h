/// @file generator.h
/// @brief Main test generation orchestrator.

#ifndef COVERWISE_CORE_GENERATOR_H_
#define COVERWISE_CORE_GENERATOR_H_

#include <cstdint>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace core {

/// @brief Options for test generation.
struct GenerateOptions {
  std::vector<model::Parameter> parameters;
  std::vector<std::string> constraint_expressions;  ///< Constraint strings
  uint32_t strength = 2;                            ///< Interaction strength (2 = pairwise)
  uint64_t seed = 0;                                ///< RNG seed for deterministic output
  uint32_t max_tests = 0;                           ///< 0 = no limit
  std::vector<model::TestCase> seeds;               ///< Existing tests to build upon
};

/// @brief Mode for extendTests operation.
enum class ExtendMode {
  kStrict,    ///< Keep existing tests exactly as-is
  kOptimize,  ///< Allow reorganizing existing tests for better coverage
};

/// @brief Generate a covering array for the given options.
/// @return The generated test suite with coverage metadata, stats, and suggestions.
model::GenerateResult Generate(const GenerateOptions& options);

/// @brief Extend an existing test suite to improve coverage.
model::GenerateResult Extend(const std::vector<model::TestCase>& existing,
                             const GenerateOptions& options, ExtendMode mode = ExtendMode::kStrict);

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_GENERATOR_H_
