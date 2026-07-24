/// @file constraint_validator.h
/// @brief Validate that generated test cases satisfy all constraints.

#ifndef COVERWISE_VALIDATOR_CONSTRAINT_VALIDATOR_H_
#define COVERWISE_VALIDATOR_CONSTRAINT_VALIDATOR_H_

#include <cstdint>
#include <vector>

#include "model/constraint_ast.h"
#include "model/test_case.h"

namespace coverwise {
namespace validator {

/// @brief Constraint validation report.
///
/// This aggregate report matches the TypeScript ConstraintReport
/// (validateConstraintReport) field-for-field, with per-test violation semantics
/// (a test that violates any constraint is counted once).
///
/// ValidateConstraints is an internal building block — it runs as part of
/// generation and is available to native/C++ embedders, but it is not a
/// standalone public entry point. The WASM binding exposes only generate,
/// analyzeCoverage, extendTests, and estimateModel; constraint validation is
/// reached through those (e.g. analyzeCoverage reports constraint-invalid rows),
/// not via a dedicated exported function.
struct ConstraintReport {
  uint32_t total_tests = 0;
  uint32_t violations = 0;
  /// @brief Indices of test cases that violate constraints.
  std::vector<uint32_t> violating_indices;
};

/// @brief Validate that all test cases satisfy all constraints.
///
/// Per-test semantics: each test is examined against every constraint and
/// counted at most once (on its first violated constraint), matching the
/// TypeScript validateConstraintReport.
ConstraintReport ValidateConstraints(const std::vector<model::TestCase>& tests,
                                     const std::vector<model::Constraint>& constraints);

}  // namespace validator
}  // namespace coverwise

#endif  // COVERWISE_VALIDATOR_CONSTRAINT_VALIDATOR_H_
