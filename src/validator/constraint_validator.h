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
///
/// This is a standalone check over a suite the caller already has, not a step
/// inside generation: no shipping path calls it. Generation constructs only
/// constraint-satisfying rows, so there is no screening pass for it to be part
/// of, and a suite coming back from generate has not been through this
/// function. It is compiled into the installed library for embedders auditing a
/// suite they did not generate — a recorded one, or one from another tool — and
/// the tests use it as an independent oracle for the same reason.
///
/// It is not reachable from the WASM binding, which exports only generate,
/// analyzeCoverage, extendTests and estimateModel. A constraint-violating row
/// submitted to analyzeCoverage is reported through the coverage validator's own
/// per-row check rather than through this function, so the two report such a row
/// separately and neither is evidence about the other.
ConstraintReport ValidateConstraints(const std::vector<model::TestCase>& tests,
                                     const std::vector<model::Constraint>& constraints);

}  // namespace validator
}  // namespace coverwise

#endif  // COVERWISE_VALIDATOR_CONSTRAINT_VALIDATOR_H_
