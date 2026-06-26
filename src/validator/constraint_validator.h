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
/// This aggregate report is the canonical, cross-surface shape: it matches the
/// TypeScript ConstraintReport (validateConstraintReport) field-for-field, with
/// per-test violation semantics (a test that violates any constraint is counted
/// once). Per-violation detail (which specific constraint a test violated) is a
/// TypeScript-only convenience exposed via validateConstraints; it is not part
/// of the canonical surface and the WASM binding intentionally exports only the
/// aggregate report.
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
