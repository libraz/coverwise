#include "validator/coverage_validator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
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
  // Vacuous coverage: nothing to cover means everything is covered.
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.uncovered.empty());
}

// ---------------------------------------------------------------------------
// 7. OutOfBoundsValueIndex
//
// A TestCase contains a value index >= the parameter's value count.
// The validator should handle this gracefully: the out-of-bounds value
// simply won't match any expected tuple, so those tuples remain uncovered.
// The validator must not crash or produce undefined behavior.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, OutOfBoundsValueIndex) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  // Test case with B=99 (out of bounds: only 0 and 1 are valid indices).
  std::vector<TestCase> tests = {
      TestCase{{0, 99}},
  };

  auto report = ValidateCoverage(params, tests, 2);

  // The validator should enumerate all 4 tuples.
  EXPECT_EQ(report.total_tuples, 4u);
  // The out-of-bounds value (B=99) cannot match any expected tuple (B=0 or B=1),
  // so no tuples should be covered.
  EXPECT_EQ(report.covered_tuples, 0u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 0.0);
  EXPECT_EQ(report.uncovered.size(), 4u);
}

// ---------------------------------------------------------------------------
// 8. ThreeWiseCoverage
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
      TestCase{{0, 0, 0}}, TestCase{{0, 0, 1}}, TestCase{{0, 1, 0}}, TestCase{{0, 1, 1}},
      TestCase{{1, 0, 0}}, TestCase{{1, 0, 1}}, TestCase{{1, 1, 0}}, TestCase{{1, 1, 1}},
  };

  auto report = ValidateCoverage(params, tests, 3);

  EXPECT_EQ(report.total_tuples, 8u);
  EXPECT_EQ(report.covered_tuples, 8u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.uncovered.empty());
}

// ---------------------------------------------------------------------------
// LargeValueProductNoOverflow
//
// The per-combination value product is accumulated in 64-bit. Two parameters
// with 1000 values each yield 1,000,000 tuples for the single pairwise
// combination; the validator must count every tuple exactly (no silent wrap)
// and report the empty suite as fully uncovered.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, LargeValueProductNoOverflow) {
  std::vector<std::string> values;
  for (int j = 0; j < 1000; ++j) {
    values.push_back(std::to_string(j));
  }
  std::vector<Parameter> params = {
      Parameter{"A", values},
      Parameter{"B", values},
  };

  std::vector<TestCase> tests;  // Empty suite: everything uncovered.

  auto report = ValidateCoverage(params, tests, 2);

  // C(2,2) = 1 combination, 1000 * 1000 = 1,000,000 tuples.
  EXPECT_EQ(report.total_tuples, 1000000u);
  EXPECT_EQ(report.covered_tuples, 0u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 0.0);
  EXPECT_EQ(report.uncovered_count, 1000000u);
  EXPECT_EQ(report.uncovered.size(), 1000u);
  EXPECT_EQ(report.omitted_uncovered, 999000u);
}

TEST(CoverageValidatorTest, RejectsUint64TupleProductWrapBeforeEnumeration) {
  std::vector<Parameter> params;
  for (uint32_t pi = 0; pi < 8; ++pi) {
    Parameter param;
    param.name = "P" + std::to_string(pi);
    for (uint32_t vi = 0; vi < 256; ++vi) param.values.push_back(std::to_string(vi));
    params.push_back(std::move(param));
  }
  auto report = ValidateCoverage(params, {}, 8);
  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_EQ(report.total_tuples, 0u);
  EXPECT_NE(report.coverage_ratio, 1.0);
}

TEST(CoverageValidatorTest, RejectsJustAboveTupleLimit) {
  std::vector<Parameter> params = {
      {"A", std::vector<std::string>(4000, "a"), {}},
      {"B", std::vector<std::string>(4001, "b"), {}},
  };
  auto report = ValidateCoverage(params, {}, 2);
  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kTupleExplosion);
}

TEST(CoverageValidatorTest, RejectsCombinationMetadataBeforeMaterialization) {
  std::vector<Parameter> params;
  for (uint32_t pi = 0; pi < 200; ++pi) {
    params.push_back({"P" + std::to_string(pi), {"only"}, {}});
  }
  auto report = ValidateCoverage(params, {}, 3);
  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kTupleExplosion);
}

// ---------------------------------------------------------------------------
// Constraint-aware coverage analysis
// ---------------------------------------------------------------------------

TEST(CoverageValidatorTest, ConstraintsExcludeTuplesFromUniverse) {
  // Two parameters, 2 values each: 4 tuples total.
  //   os       = {win, mac}
  //   browser  = {chrome, ie}
  // Constraint: IF os=mac THEN browser!=ie
  //   -> excludes (os=mac, browser=ie) from the universe, leaving 3 valid tuples.
  std::vector<Parameter> params = {
      Parameter{"os", {"win", "mac"}},
      Parameter{"browser", {"chrome", "ie"}},
  };

  auto parse = coverwise::model::ParseConstraint("IF os=mac THEN browser!=ie", params);
  ASSERT_TRUE(parse.error.ok()) << parse.error.message << ": " << parse.error.detail;
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::move(parse.constraint));

  // Provide 2 tests covering 2 of the 3 valid tuples.
  //   {win, chrome} and {mac, chrome}
  // Missing valid tuple: {win, ie}.
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},  // win, chrome
      TestCase{{1, 0}},  // mac, chrome
  };

  auto report = ValidateCoverage(params, tests, 2, constraints);

  EXPECT_EQ(report.total_tuples, 3u);
  EXPECT_EQ(report.covered_tuples, 2u);
  ASSERT_EQ(report.uncovered.size(), 1u);
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"os=win", "browser=ie"}));
}

// ---------------------------------------------------------------------------
// Invalid-value exclusion (oracle/generator agreement)
//
// A model with an invalid value must have all tuples containing that value
// excluded from the coverage universe, matching the generator's
// CoverageEngine::ExcludeInvalidValues. A suite that covers every valid tuple
// must report coverage_ratio == 1.0 with an empty uncovered list, and the
// invalid-value tuples must never appear in total_tuples or uncovered.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, InvalidValuesExcludedFromUniverse) {
  // os = {win, mac, ie6(invalid)}, browser = {chrome, safari}.
  // Valid pairs: (win|mac) x (chrome|safari) = 4. Tuples involving os=ie6
  // (2 of them: ie6/chrome, ie6/safari) are excluded.
  std::vector<Parameter> params = {
      Parameter{"os", {"win", "mac", "ie6"}, {false, false, true}},
      Parameter{"browser", {"chrome", "safari"}, {false, false}},
  };

  // Suite achieving full valid coverage (no invalid value referenced).
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},  // win, chrome
      TestCase{{0, 1}},  // win, safari
      TestCase{{1, 0}},  // mac, chrome
      TestCase{{1, 1}},  // mac, safari
  };

  auto report = ValidateCoverage(params, tests, 2);

  // Only the 4 valid pairs are in the universe; ie6-based tuples excluded.
  EXPECT_EQ(report.total_tuples, 4u);
  EXPECT_EQ(report.covered_tuples, 4u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.uncovered.empty());

  // The invalid-value tuples must never surface as uncovered.
  EXPECT_FALSE(UncoveredContains(report.uncovered, {"os=ie6", "browser=chrome"}));
  EXPECT_FALSE(UncoveredContains(report.uncovered, {"os=ie6", "browser=safari"}));
}

TEST(CoverageValidatorTest, ExcludesTupleWithoutCompleteConstraintWitness) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expression : {"IF A=0 THEN C=0", "IF B=0 THEN C=1"}) {
    auto parsed = coverwise::model::ParseConstraint(expression, params);
    ASSERT_TRUE(parsed.error.ok());
    constraints.push_back(std::move(parsed.constraint));
  }

  std::vector<TestCase> tests = {
      {{0, 1, 0}},
      {{1, 0, 1}},
      {{1, 1, 0}},
      {{1, 1, 1}},
  };
  auto report = ValidateCoverage(params, tests, 2, constraints);

  EXPECT_EQ(report.total_tuples, 9u);
  EXPECT_FALSE(UncoveredContains(report.uncovered, {"A=0", "B=0"}));
}

TEST(CoverageValidatorTest, MalformedAndConstraintViolatingTestsDoNotContribute) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  auto parsed = coverwise::model::ParseConstraint("IF A=0 THEN C=0", params);
  ASSERT_TRUE(parsed.error.ok());
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::move(parsed.constraint));

  // Both rows match A=0,B=0, but one is missing C and one violates C=0.
  std::vector<TestCase> tests = {{{0, 0}}, {{0, 0, 1}}};
  auto report = ValidateCoverage(params, tests, 2, constraints);

  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=0", "B=0"}));
}
