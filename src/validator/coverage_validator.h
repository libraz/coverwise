/// @file coverage_validator.h
/// @brief Independent coverage validation (does NOT depend on generator/core).

#ifndef COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_
#define COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_

#include <cstdint>
#include <vector>

#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace validator {

/// @brief Coverage validation report with human-readable uncovered tuples.
struct CoverageReport {
  uint32_t total_tuples = 0;
  uint32_t covered_tuples = 0;
  double coverage_ratio = 0.0;
  /// @brief Human-readable uncovered tuples (e.g. "os=win, browser=safari").
  std::vector<model::UncoveredTuple> uncovered;
};

/// @brief Equivalence class coverage report.
struct ClassCoverageReport {
  uint32_t total_class_tuples = 0;
  uint32_t covered_class_tuples = 0;
  double coverage_ratio = 0.0;
};

/// @brief Independently validate t-wise coverage of a test suite.
///
/// This validator enumerates all t-tuples from scratch (not using any
/// generator internals) and checks each against the test suite.
CoverageReport ValidateCoverage(const std::vector<model::Parameter>& params,
                                const std::vector<model::TestCase>& tests, uint32_t strength);

/// @brief Compute equivalence class coverage for a test suite.
///
/// Maps each value to its equivalence class and enumerates all t-wise class
/// tuples, counting how many are covered by the test suite.
/// Only considers parameters that have equivalence classes defined.
/// @return Class coverage report. If no parameters have classes, returns all zeros.
ClassCoverageReport ComputeClassCoverage(const std::vector<model::Parameter>& params,
                                         const std::vector<model::TestCase>& tests,
                                         uint32_t strength);

}  // namespace validator
}  // namespace coverwise

#endif  // COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_
