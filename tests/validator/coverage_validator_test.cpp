#include "validator/coverage_validator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "support/allocation_counter.h"

using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::test_support::AllocationCounter;
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
  for (const auto& uncovered : report.uncovered) {
    EXPECT_EQ(uncovered.indices.size(), 2u);
  }
}

TEST(CoverageValidatorTest, UncoveredIndicesPreserveNamesAndValuesContainingEquals) {
  std::vector<Parameter> params = {{"A=B", {"x=y"}, {}}, {"C", {"z"}, {}}};
  const auto report = ValidateCoverage(params, {}, 2);

  ASSERT_EQ(report.uncovered.size(), 1u);
  EXPECT_EQ(report.uncovered[0].tuple, (std::vector<std::string>{"A=B=x=y", "C=z"}));
  EXPECT_EQ(report.uncovered[0].indices,
            (std::vector<std::pair<uint32_t, uint32_t>>{{0, 0}, {1, 0}}));
}

TEST(CoverageValidatorTest, ContradictoryConstraintsReturnErrorInsteadOfFullCoverage) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expression : {"A=0", "A!=0"}) {
    auto parsed = coverwise::model::ParseConstraint(expression, params);
    ASSERT_TRUE(parsed.error.ok()) << parsed.error.message;
    constraints.push_back(std::move(parsed.constraint));
  }

  auto report = ValidateCoverage(params, {}, 2, constraints);

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(report.error.message, "Constraints are unsatisfiable");
  EXPECT_NE(report.coverage_ratio, 1.0);
}

TEST(CoverageValidatorTest, AllInvalidParameterReturnsInvalidInput) {
  std::vector<Parameter> params = {
      Parameter{"A", {"0", "1"}, {true, true}},
      Parameter{"B", {"0", "1"}, {}},
  };

  auto report = ValidateCoverage(params, {}, 2);

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_NE(report.coverage_ratio, 1.0);
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
// strength (3) > number of parameters (2) is invalid input — the validator
// rejects it (matching generate) rather than green-lighting the query with a
// vacuous 100% coverage.
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

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kInvalidInput);
}

// ---------------------------------------------------------------------------
// 7. OutOfBoundsValueIndex
//
// A TestCase contains a value index >= the parameter's value count. The row is
// rejected whole and reported in invalid_tests, so none of its values count as
// coverage — not even the in-range ones. The validator must not crash or
// produce undefined behavior.
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
  // The row is rejected whole, so A=0 does not count either.
  EXPECT_EQ(report.covered_tuples, 0u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 0.0);
  EXPECT_EQ(report.uncovered.size(), 4u);
  ASSERT_EQ(report.invalid_tests.size(), 1u);
  EXPECT_EQ(report.invalid_tests[0].test_index, 0u);
  EXPECT_EQ(report.invalid_tests[0].reason, "value index 99 is out of range for parameter B");
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
  Parameter a{"A", {}};
  Parameter b{"B", {}};
  for (uint32_t index = 0; index < 4000; ++index) a.values.push_back("a" + std::to_string(index));
  for (uint32_t index = 0; index < 4001; ++index) b.values.push_back("b" + std::to_string(index));
  std::vector<Parameter> params = {std::move(a), std::move(b)};
  auto report = ValidateCoverage(params, {}, 2);
  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kTupleExplosion);
}

TEST(CoverageValidatorTest, RejectsAParameterCountBeyondTheDocumentedLimit) {
  // Feasibility search walks one parameter per level, so an oversized model has
  // to be turned away as invalid input before any search starts.
  constexpr uint32_t kParameters = 200'000;
  std::vector<Parameter> params;
  params.reserve(kParameters);
  for (uint32_t index = 0; index < kParameters; ++index) {
    params.push_back({"P" + std::to_string(index), {"a", "b"}});
  }
  auto parse = coverwise::model::ParseConstraint("P0=\"a\" OR P1=\"b\"", params);
  ASSERT_TRUE(parse.error.ok()) << parse.error.message;
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::move(parse.constraint));

  auto report = ValidateCoverage(params, {}, 1, constraints);

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_EQ(report.total_tuples, 0u);
}

TEST(CoverageValidatorTest, AcceptsTheLargestDocumentedParameterCount) {
  constexpr uint32_t kParameters = coverwise::model::kMaxParameters;
  std::vector<Parameter> params;
  params.reserve(kParameters);
  for (uint32_t index = 0; index < kParameters; ++index) {
    params.push_back({"P" + std::to_string(index), {"a", "b"}});
  }
  auto parse = coverwise::model::ParseConstraint("P0=\"a\" OR P1=\"b\"", params);
  ASSERT_TRUE(parse.error.ok()) << parse.error.message;
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::move(parse.constraint));

  auto report = ValidateCoverage(params, {}, 1, constraints);

  EXPECT_TRUE(report.error.ok()) << report.error.message << ": " << report.error.detail;
  EXPECT_EQ(report.total_tuples, 2u * kParameters);
}

TEST(CoverageValidatorTest, RejectsOneParameterPastTheLimit) {
  std::vector<Parameter> params;
  params.reserve(coverwise::model::kMaxParameters + 1);
  for (size_t index = 0; index <= coverwise::model::kMaxParameters; ++index) {
    params.push_back({"P" + std::to_string(index), {"a", "b"}});
  }

  auto report = ValidateCoverage(params, {}, 1);

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_EQ(report.error.message, "Parameter count 1025 exceeds maximum of 1024");
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

// ---------------------------------------------------------------------------
// Constrained validation on a covering suite
//
// A tuple a valid test already covers comes with its own completion witness:
// that test is a full assignment of valid values satisfying every constraint.
// Re-deriving it with a feasibility search costs the whole model per tuple, so
// the validator must not run one. These tests pin the two consequences — the
// report is unchanged, and the constrained run stays close to the unconstrained
// one in both time and heap traffic.
// ---------------------------------------------------------------------------
namespace {

std::vector<Parameter> UniformParams(uint32_t count, uint32_t values) {
  std::vector<Parameter> params;
  for (uint32_t i = 0; i < count; ++i) {
    std::vector<std::string> names;
    for (uint32_t v = 0; v < values; ++v) names.push_back("v" + std::to_string(v));
    params.emplace_back("p" + std::to_string(i), names);
  }
  return params;
}

/// @brief Strength-2 covering array for binary parameters.
///
/// The all-zero and all-one rows cover the (0,0) and (1,1) pairs; for any two
/// distinct parameters some bit of their indices differs, so the row carrying
/// that bit and its complement cover (0,1) and (1,0).
std::vector<TestCase> BinaryCoveringSuite(uint32_t count) {
  std::vector<TestCase> tests(2);
  tests[0].values.assign(count, 0);
  tests[1].values.assign(count, 1);
  for (uint32_t bit = 0; (1u << bit) < count; ++bit) {
    TestCase row;
    TestCase complement;
    row.values.resize(count);
    complement.values.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t value = (i >> bit) & 1u;
      row.values[i] = value;
      complement.values[i] = 1u - value;
    }
    tests.push_back(row);
    tests.push_back(complement);
  }
  return tests;
}

/// @brief Every assignment whose value for parameter i runs over [lowest[i], size).
std::vector<TestCase> ExhaustiveSuite(const std::vector<Parameter>& params,
                                      const std::vector<uint32_t>& lowest) {
  std::vector<TestCase> tests;
  TestCase current;
  current.values = lowest;
  for (;;) {
    tests.push_back(current);
    size_t pos = params.size();
    bool carry = true;
    while (pos > 0 && carry) {
      --pos;
      if (++current.values[pos] < params[pos].size()) {
        carry = false;
      } else {
        current.values[pos] = lowest[pos];
      }
    }
    if (carry) return tests;
  }
}

std::vector<coverwise::model::Constraint> ParseAll(const std::vector<Parameter>& params,
                                                   const std::vector<std::string>& expressions) {
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expression : expressions) {
    auto parsed = coverwise::model::ParseConstraint(expression, params);
    EXPECT_TRUE(parsed.error.ok()) << expression << ": " << parsed.error.message;
    constraints.push_back(std::move(parsed.constraint));
  }
  return constraints;
}

/// @brief One timed ValidateCoverage run, in milliseconds.
double ValidationMs(const std::vector<Parameter>& params, const std::vector<TestCase>& tests,
                    uint32_t strength,
                    const std::vector<coverwise::model::Constraint>& constraints) {
  auto start = std::chrono::steady_clock::now();
  auto report = ValidateCoverage(params, tests, strength, constraints);
  auto elapsed =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  EXPECT_TRUE(report.error.ok());
  return elapsed;
}

/// @brief Fastest unconstrained and constrained run over one model, sampled
///        alternately.
///
/// Timing one to completion and only then the other lets a shift in machine load
/// land wholly on whichever went second, which reads back as a ratio the model
/// did nothing to earn. Alternating puts both through the same load window, and
/// swapping which one leads on alternate rounds keeps the cost of the round
/// itself from being charged to the same one every time. Taking each one's own
/// minimum then keeps the quietest of the windows.
std::pair<double, double> FastestValidationMsEach(
    const std::vector<Parameter>& params, const std::vector<TestCase>& tests, uint32_t strength,
    const std::vector<coverwise::model::Constraint>& constraints, int repetitions) {
  const std::vector<coverwise::model::Constraint> none;
  double unconstrained_best = 0.0;
  double constrained_best = 0.0;
  for (int i = 0; i < repetitions; ++i) {
    const bool unconstrained_leads = (i % 2) == 0;
    double unconstrained_ms =
        unconstrained_leads ? ValidationMs(params, tests, strength, none) : 0.0;
    double constrained_ms = ValidationMs(params, tests, strength, constraints);
    if (!unconstrained_leads) unconstrained_ms = ValidationMs(params, tests, strength, none);
    if (i == 0 || unconstrained_ms < unconstrained_best) unconstrained_best = unconstrained_ms;
    if (i == 0 || constrained_ms < constrained_best) constrained_best = constrained_ms;
  }
  return {unconstrained_best, constrained_best};
}

}  // namespace

TEST(CoverageValidatorTest, SuccessfulFeasibilitySearchLeavesTheScratchAssignmentUntouched) {
  // The tuple loop hands its own assignment buffer to the feasibility search
  // instead of copying it per tuple, so a search that finds a witness has to
  // undo its own writes. Leaking them would make later tuples be judged against
  // the previous witness rather than against themselves.
  //
  // A=0,B=0 is feasible and its first witness assigns C=0. The very next tuple,
  // A=0,B=1, is feasible only with C=1 — so a leaked C=0 turns it infeasible
  // and drops it out of the universe.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  auto constraints = ParseAll(params, {"IF B=1 THEN C=1"});

  auto report = ValidateCoverage(params, {}, 2, constraints);

  ASSERT_TRUE(report.error.ok()) << report.error.message;
  // (A,B) and (A,C) contribute 4 tuples each; (B,C) loses only (B=1, C=0).
  EXPECT_EQ(report.total_tuples, 11u);
  EXPECT_EQ(report.uncovered_count, 11u);
  EXPECT_TRUE(UncoveredContains(report.uncovered, {"A=0", "B=1"}));
  EXPECT_FALSE(UncoveredContains(report.uncovered, {"B=1", "C=0"}));
}

TEST(CoverageValidatorTest, TriviallySatisfiedConstraintReportsSameUniverseAsNoConstraint) {
  // 500 binary parameters: 124,750 parameter pairs, 499,000 tuples. The
  // constraint holds for every assignment of p0, so it excludes nothing and the
  // two reports have to agree field for field.
  auto params = UniformParams(500, 2);
  auto tests = BinaryCoveringSuite(500);
  auto constraints = ParseAll(params, {"p0 = v0 OR p0 = v1"});

  auto unconstrained = ValidateCoverage(params, tests, 2);
  auto constrained = ValidateCoverage(params, tests, 2, constraints);

  EXPECT_EQ(constrained.total_tuples, unconstrained.total_tuples);
  EXPECT_EQ(constrained.covered_tuples, unconstrained.covered_tuples);
  EXPECT_EQ(constrained.uncovered_count, unconstrained.uncovered_count);
  EXPECT_EQ(constrained.omitted_uncovered, unconstrained.omitted_uncovered);
  EXPECT_EQ(constrained.uncovered.size(), unconstrained.uncovered.size());
  EXPECT_DOUBLE_EQ(constrained.coverage_ratio, unconstrained.coverage_ratio);
  EXPECT_EQ(constrained.invalid_tests.size(), unconstrained.invalid_tests.size());
  EXPECT_TRUE(constrained.error.ok());

  // The suite covers the whole universe, which is what makes the search
  // redundant in the first place.
  EXPECT_EQ(constrained.total_tuples, 499000u);
  EXPECT_EQ(constrained.covered_tuples, 499000u);
  EXPECT_EQ(constrained.uncovered_count, 0u);
  EXPECT_DOUBLE_EQ(constrained.coverage_ratio, 1.0);
}

TEST(CoverageValidatorTest, ConstrainedCoveringSuiteSkipsTheSearchForWitnessedTuples) {
  auto params = UniformParams(500, 2);
  auto tests = BinaryCoveringSuite(500);
  auto constraints = ParseAll(params, {"p0 = v0 OR p0 = v1"});

  const int kRepetitions = 3;
  auto [unconstrained_ms, constrained_ms] =
      FastestValidationMsEach(params, tests, 2, constraints, kRepetitions);

  // A tuple covered by a valid test already holds its own completion witness, so
  // it must never reach the feasibility search. That is binary: either the
  // witness is honoured and this suite — which covers its whole universe —
  // searches nothing, or it is not and all 499,000 tuples each pay for a descent
  // over 500 parameters.
  //
  // The bound is a detector for that regime change, not a runtime budget. With
  // the skip, constraints cost nothing measurable and the ratio sits at 1.0;
  // without it the same model runs some 150x the unconstrained time. 5.0 is
  // therefore far enough above the ratio to clear the contention noise of a
  // parallel sanitizer run, and far enough below the regression that no
  // plausible tightening of the constraint path could drag it over the line.
  EXPECT_LT(constrained_ms, 5.0 * unconstrained_ms)
      << "constrained=" << constrained_ms << "ms unconstrained=" << unconstrained_ms << "ms";
}

TEST(CoverageValidatorTest, ConstrainedTupleLoopAllocationCountIsIndependentOfSearchCount) {
  // Two models over the same 6x4 parameters, differing only in how many tuples
  // reach the feasibility search. Both suites cover their entire feasible
  // universe, so no uncovered diagnostic is built and every allocation left is
  // fixed setup. Equal counts therefore mean the searches allocate nothing.
  auto params = UniformParams(6, 4);

  // Only tuples containing p0=v0 are infeasible: 240 - 220 = 20 searches.
  auto narrow = ParseAll(params, {"p0 != v0"});
  std::vector<uint32_t> narrow_lowest(6, 0);
  narrow_lowest[0] = 1;
  auto narrow_tests = ExhaustiveSuite(params, narrow_lowest);

  // Every tuple containing v0 anywhere is infeasible: 240 - 135 = 105 searches.
  auto broad = ParseAll(params, {"p0 != v0 AND p1 != v0 AND p2 != v0 AND p3 != v0 AND "
                                 "p4 != v0 AND p5 != v0"});
  auto broad_tests = ExhaustiveSuite(params, std::vector<uint32_t>(6, 1));

  AllocationCounter narrow_counter;
  auto narrow_report = ValidateCoverage(params, narrow_tests, 2, narrow);
  uint64_t narrow_allocations = narrow_counter.Stop();

  AllocationCounter broad_counter;
  auto broad_report = ValidateCoverage(params, broad_tests, 2, broad);
  uint64_t broad_allocations = broad_counter.Stop();

  ASSERT_TRUE(narrow_report.error.ok());
  ASSERT_TRUE(broad_report.error.ok());
  EXPECT_EQ(narrow_report.total_tuples, 220u);
  EXPECT_EQ(narrow_report.covered_tuples, 220u);
  EXPECT_EQ(narrow_report.uncovered_count, 0u);
  EXPECT_EQ(broad_report.total_tuples, 135u);
  EXPECT_EQ(broad_report.covered_tuples, 135u);
  EXPECT_EQ(broad_report.uncovered_count, 0u);

  EXPECT_EQ(broad_allocations, narrow_allocations)
      << "broad=" << broad_allocations << " narrow=" << narrow_allocations;
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
  // Neither row silently disappears: both are reported, in input order.
  ASSERT_EQ(report.invalid_tests.size(), 2u);
  EXPECT_EQ(report.invalid_tests[0].test_index, 0u);
  EXPECT_EQ(report.invalid_tests[1].test_index, 1u);
}

// ---------------------------------------------------------------------------
// invalid_tests
//
// A row the validator refuses to count is the one thing a caller cannot see
// from the coverage numbers alone: a rejected row lowers coverage exactly like
// a missing one. Every rejection category therefore has to name the row and say
// why, and the five categories are checked in a fixed order — arity first, then
// per-parameter unassigned / out of range / marked invalid, then the
// constraints.
// ---------------------------------------------------------------------------
TEST(CoverageValidatorTest, InvalidTestsNameEveryRejectedRowAndItsReason) {
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "bad"}, {false, true}},
      {"C", {"c0", "c1"}, {}},
  };
  auto parsed = coverwise::model::ParseConstraint("IF A=a0 THEN C=c0", params);
  ASSERT_TRUE(parsed.error.ok());
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::move(parsed.constraint));

  const uint32_t unassigned = coverwise::model::kUnassigned;
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},              // 0: too few values
      TestCase{{0, unassigned, 0}},  // 1: B left unassigned
      TestCase{{0, 99, 0}},          // 2: B out of range
      TestCase{{0, 1, 0}},           // 3: B=bad is marked invalid
      TestCase{{0, 0, 1}},           // 4: violates the constraint
      TestCase{{0, 0, 0}},           // 5: accepted
      TestCase{{0, 0, 0, 0}},        // 6: too many values
  };

  auto report = ValidateCoverage(params, tests, 2, constraints);
  ASSERT_TRUE(report.error.ok()) << report.error.message;

  ASSERT_EQ(report.invalid_tests.size(), 6u);

  EXPECT_EQ(report.invalid_tests[0].test_index, 0u);
  EXPECT_EQ(report.invalid_tests[0].reason, "expected 3 value(s), got 2");

  EXPECT_EQ(report.invalid_tests[1].test_index, 1u);
  EXPECT_EQ(report.invalid_tests[1].reason, "missing value for parameter B");

  EXPECT_EQ(report.invalid_tests[2].test_index, 2u);
  EXPECT_EQ(report.invalid_tests[2].reason, "value index 99 is out of range for parameter B");

  EXPECT_EQ(report.invalid_tests[3].test_index, 3u);
  EXPECT_EQ(report.invalid_tests[3].reason, "value B=bad is marked invalid");

  EXPECT_EQ(report.invalid_tests[4].test_index, 4u);
  EXPECT_EQ(report.invalid_tests[4].reason,
            "violates constraint #1 (constraint evaluation is false or indeterminate)");

  EXPECT_EQ(report.invalid_tests[5].test_index, 6u);
  EXPECT_EQ(report.invalid_tests[5].reason, "expected 3 value(s), got 4");

  // Only row 5 survived, so it is the only one that can contribute coverage.
  // A=a0,B=b0 is the pair it covers; the invalid-value tuples are outside the
  // universe entirely.
  EXPECT_EQ(report.covered_tuples, 3u);
}

// ---------------------------------------------------------------------------
// Feasibility search budget
//
// The search is bounded so a hard model terminates, which leaves two answers
// that look alike from the outside and must not: a search that finished and
// found nothing, and one that ran out of nodes. Both exits report the budget
// explicitly, and an undecided tuple is counted in no bucket at all.
// ---------------------------------------------------------------------------
namespace {

using coverwise::model::ConstraintNode;
using coverwise::model::ConstraintResult;
using coverwise::model::kUnassigned;

/// @brief Undecided until every parameter is assigned, and rejecting at the
///        leaf, so concluding anything costs a sweep of the whole space.
class LateRejectConstraint final : public ConstraintNode {
 public:
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override {
    for (uint32_t value : assignment) {
      if (value == kUnassigned) return ConstraintResult::kUnknown;
    }
    return ConstraintResult::kFalse;
  }
};

/// @brief Satisfied as soon as the first parameter takes its first value, and a
///        LateRejectConstraint otherwise.
///
/// The whole-model search tries first values first and so settles at once,
/// while a tuple pinning that parameter to its second value pays the sweep.
class GatedLateRejectConstraint final : public ConstraintNode {
 public:
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override {
    if (assignment[0] == kUnassigned) return ConstraintResult::kUnknown;
    if (assignment[0] == 0) return ConstraintResult::kTrue;
    for (uint32_t value : assignment) {
      if (value == kUnassigned) return ConstraintResult::kUnknown;
    }
    return ConstraintResult::kFalse;
  }
};

}  // namespace

TEST(CoverageValidatorTest, ExhaustedModelSearchIsBudgetExceededNotUnsatisfiable) {
  // The constraint can only be decided at a leaf, and 22 binary parameters put
  // more leaves below the root than the node budget covers. "Constraints are
  // unsatisfiable" would claim a proof the search never finished.
  auto params = UniformParams(22, 2);
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::make_unique<LateRejectConstraint>());

  auto report = ValidateCoverage(params, {}, 2, constraints);

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(report.error.message, "Constraint search budget exceeded");
  EXPECT_EQ(report.error.detail,
            "The constraint model is too complex to solve within the search budget");
  EXPECT_EQ(report.total_tuples, 0u);
}

TEST(CoverageValidatorTest, ExhaustedTupleSearchStopsValidationWithoutCountingTheTuple) {
  // The model is satisfiable at the first assignment tried, so validation
  // reaches the tuple loop; the tuples pinning p0 to its second value then cost
  // a sweep of the remaining 22 parameters, which the node budget does not
  // cover.
  auto params = UniformParams(24, 2);
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::make_unique<GatedLateRejectConstraint>());

  auto report = ValidateCoverage(params, {}, 2, constraints);

  EXPECT_EQ(report.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(report.error.message, "Constraint search budget exceeded");
  EXPECT_EQ(report.error.detail,
            "Tuple feasibility could not be determined within the search budget");

  // Validation stops on the undecided tuple. The two (p0=v0, p1=*) tuples ahead
  // of it are counted, and the undecided one is counted as neither covered,
  // uncovered nor excluded.
  EXPECT_EQ(report.total_tuples, 2u);
  EXPECT_EQ(report.covered_tuples, 0u);
  EXPECT_EQ(report.uncovered_count, 2u);
}

TEST(CoverageValidatorTest, RejectionReasonNamesTheValueTheRowCarried) {
  // A recorded row that names a value the model no longer declares keeps
  // kUnassigned at that position, so the caller's own text is the only thing
  // left that says which member of the row drifted. Naming the parameter alone
  // does not distinguish it from a row that omitted the member entirely.
  std::vector<Parameter> params = {
      {"browser", {"chrome", "firefox"}, {}},
      {"os", {"linux", "mac"}, {}},
  };
  TestCase drifted;
  drifted.values = {coverwise::model::kUnassigned, 0};
  drifted.unresolved = {"edge", ""};
  TestCase omitted;
  omitted.values = {coverwise::model::kUnassigned, 0};

  auto report = ValidateCoverage(params, {drifted, omitted}, 2);

  ASSERT_EQ(report.invalid_tests.size(), 2u);
  EXPECT_EQ(report.invalid_tests[0].reason, "value 'edge' is not declared by parameter browser");
  EXPECT_EQ(report.invalid_tests[1].reason, "missing value for parameter browser");
}

TEST(CoverageValidatorTest, InvalidTestsIsEmptyWhenEveryRowIsAccepted) {
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  std::vector<TestCase> tests = {TestCase{{0, 0}}, TestCase{{1, 1}}};

  auto report = ValidateCoverage(params, tests, 2);

  ASSERT_TRUE(report.error.ok());
  EXPECT_TRUE(report.invalid_tests.empty());
}
