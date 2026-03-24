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
struct ConstraintReport {
  uint32_t total_tests = 0;
  uint32_t violations = 0;
  /// @brief Indices of test cases that violate constraints.
  std::vector<uint32_t> violating_indices;
};

/// @brief Validate that all test cases satisfy all constraints.
ConstraintReport ValidateConstraints(const std::vector<model::TestCase>& tests,
                                     const std::vector<model::Constraint>& constraints);

}  // namespace validator
}  // namespace coverwise

#endif  // COVERWISE_VALIDATOR_CONSTRAINT_VALIDATOR_H_
