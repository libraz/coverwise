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

/// @brief Independently validate t-wise coverage of a test suite.
///
/// This validator enumerates all t-tuples from scratch (not using any
/// generator internals) and checks each against the test suite.
CoverageReport ValidateCoverage(const std::vector<model::Parameter>& params,
                                const std::vector<model::TestCase>& tests, uint32_t strength);

}  // namespace validator
}  // namespace coverwise

#endif  // COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_
