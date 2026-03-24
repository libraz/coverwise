#include "validator/coverage_validator.h"

#include <algorithm>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "model/parameter.h"
#include "model/test_case.h"

using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::validator::ValidateCoverage;

// ---------------------------------------------------------------------------
// Helper: build a UncoveredTuple key string from a tuple's entries
// ---------------------------------------------------------------------------
static std::string TupleKey(const coverwise::model::UncoveredTuple& t) {
  std::string key;
  for (const auto& entry : t.tuple) {
    if (!key.empty()) key += ",";
    key += entry;
  }
  return key;
}

static bool UncoveredContains(const std::vector<coverwise::model::UncoveredTuple>& uncovered,
                               const std::vector<std::string>& expected_entries) {
  std::string target;
  for (const auto& e : expected_entries) {
    if (!target.empty()) target += ",";
    target += e;
  }
  for (const auto& t : uncovered) {
    if (TupleKey(t) == target) return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// 1. EmptyTestSuiteZeroCoverage
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, EmptyTestSuiteZeroCoverage) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  std::vector<TestCase> tests;

  auto report = ValidateCoverage(params, tests, 2);

  // 2 binary params, strength=2: C(2,2) * 2*2 = 4 tuples.
  EXPECT_EQ(report.total_tuples, 4u);
  EXPECT_EQ(report.covered_tuples, 0u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 0.0);
  EXPECT_EQ(report.uncovered.size(), 4u);
}

// ---------------------------------------------------------------------------
// 2. FullCoverageTwoBinaryParams
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, FullCoverageTwoBinaryParams) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  // All 4 combinations cover all 4 (A,B) pairs.
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},
      TestCase{{0, 1}},
      TestCase{{1, 0}},
      TestCase{{1, 1}},
  };

  auto report = ValidateCoverage(params, tests, 2);

  EXPECT_EQ(report.total_tuples, 4u);
  EXPECT_EQ(report.covered_tuples, 4u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.uncovered.empty());
}

// ---------------------------------------------------------------------------
// 3. ThreeBinaryParamsPairwise
//
// 3 binary params: C(3,2)*4 = 12 pairs.
// The 4 test cases {0,0,0}, {0,1,1}, {1,0,1}, {1,1,0} form a known
// covering array CA(4; 2, 3, 2) and achieve 100% pairwise coverage.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, ThreeBinaryParamsPairwise) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  std::vector<TestCase> tests = {
      TestCase{{0, 0, 0}},
      TestCase{{0, 1, 1}},
      TestCase{{1, 0, 1}},
      TestCase{{1, 1, 0}},
  };

  auto report = ValidateCoverage(params, tests, 2);

  EXPECT_EQ(report.total_tuples, 12u);
  EXPECT_EQ(report.covered_tuples, 12u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.uncovered.empty());
}

// ---------------------------------------------------------------------------
// 4. PartialCoverageReportsCorrectUncovered
//
// 2 params A={v0,v1,v2} B={v0,v1,v2}: 9 (A,B) pairs total.
// Provide only tests {0,0} and {1,1}: covers pairs (A=v0,B=v0) and
// (A=v1,B=v1).  The remaining 7 pairs must appear in uncovered.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, PartialCoverageReportsCorrectUncovered) {
  std::vector<Parameter> params = {
      {"A", {"v0", "v1", "v2"}, {}},
      {"B", {"v0", "v1", "v2"}, {}},
  };
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},  // covers (A=v0, B=v0)
      TestCase{{1, 1}},  // covers (A=v1, B=v1)
  };

  auto report = ValidateCoverage(params, tests, 2);

  EXPECT_EQ(report.total_tuples, 9u);
  EXPECT_EQ(report.covered_tuples, 2u);
  EXPECT_LT(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 7u);

  // Verify that the two covered pairs do NOT appear in uncovered.
  EXPECT_FALSE(UncoveredContains(report.uncovered, {"A=v0", "B=v0"}));
  EXPECT_FALSE(UncoveredContains(report.uncovered, {"A=v1", "B=v1"}));

  // Verify a selection of the expected uncovered pairs are present.
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v0", "B=v1"}));
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v0", "B=v2"}));
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v1", "B=v0"}));
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v1", "B=v2"}));
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v2", "B=v0"}));
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v2", "B=v1"}));
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=v2", "B=v2"}));
}

// ---------------------------------------------------------------------------
// 5. SingleParameterStrengthOne
//
// 1 param with 3 values, strength=1: 3 tuples (one per value).
// Provide tests covering value indices 0 and 1; value index 2 must be
// reported as uncovered.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, SingleParameterStrengthOne) {
  std::vector<Parameter> params = {
      {"Color", {"red", "green", "blue"}, {}},
  };
  std::vector<TestCase> tests = {
      TestCase{{0}},  // red
      TestCase{{1}},  // green
  };

  auto report = ValidateCoverage(params, tests, 1);

  EXPECT_EQ(report.total_tuples, 3u);
  EXPECT_EQ(report.covered_tuples, 2u);
  EXPECT_NEAR(report.coverage_ratio, 2.0 / 3.0, 1e-9);
  EXPECT_EQ(report.uncovered.size(), 1u);
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"Color=blue"}));
}

// ---------------------------------------------------------------------------
// 6. StrengthExceedsParamCount
//
// strength (3) > number of parameters (2): no tuples can be formed.
// The validator returns an empty report (total_tuples=0, coverage_ratio=0.0).
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, StrengthExceedsParamCount) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},
      TestCase{{1, 1}},
  };

  auto report = ValidateCoverage(params, tests, 3);

  EXPECT_EQ(report.total_tuples, 0u);
  // When total_tuples == 0 the validator early-returns without setting
  // coverage_ratio, so it remains 0.0 (not 1.0).
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 0.0);
  EXPECT_TRUE(report.uncovered.empty());
}

// ---------------------------------------------------------------------------
// 7. ThreeWiseCoverage
//
// 3 params with 2 values each, strength=3: C(3,3)*2^3 = 8 tuples.
// Providing all 8 combinations yields 100% 3-wise coverage.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, ThreeWiseCoverage) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  // All 8 = 2^3 combinations.
  std::vector<TestCase> tests = {
      TestCase{{0, 0, 0}},
      TestCase{{0, 0, 1}},
      TestCase{{0, 1, 0}},
      TestCase{{0, 1, 1}},
      TestCase{{1, 0, 0}},
      TestCase{{1, 0, 1}},
      TestCase{{1, 1, 0}},
      TestCase{{1, 1, 1}},
  };

  auto report = ValidateCoverage(params, tests, 3);

  EXPECT_EQ(report.total_tuples, 8u);
  EXPECT_EQ(report.covered_tuples, 8u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.uncovered.empty());
}
