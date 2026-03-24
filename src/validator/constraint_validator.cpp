/// @file constraint_validator.cpp

#include "validator/constraint_validator.h"

namespace coverwise {
namespace validator {

ConstraintReport ValidateConstraints(const std::vector<model::TestCase>& tests,
                                     const std::vector<model::Constraint>& constraints) {
  ConstraintReport report;
  report.total_tests = static_cast<uint32_t>(tests.size());

  for (uint32_t i = 0; i < report.total_tests; ++i) {
    for (const auto& constraint : constraints) {
      auto result = constraint->Evaluate(tests[i].values);
      if (result == model::ConstraintResult::kFalse) {
        report.violations++;
        report.violating_indices.push_back(i);
        break;
      }
    }
  }

  return report;
}

}  // namespace validator
}  // namespace coverwise
