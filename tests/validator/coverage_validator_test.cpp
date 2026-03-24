#include <gtest/gtest.h>

#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/coverage_validator.h"

using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::validator::ValidateCoverage;

TEST(CoverageValidatorTest, EmptyTestSuiteZeroCoverage) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
  };
  std::vector<TestCase> tests;

  auto report = ValidateCoverage(params, tests, 2);
  // TODO: Once implemented, verify total_tuples = 4, covered = 0
}

// TODO: Add tests once validator is implemented:
// - Full coverage for known optimal covering arrays
// - Partial coverage detection
// - Single parameter (no pairs)
// - 3 binary params: 4 test cases should give 100% pairwise coverage
