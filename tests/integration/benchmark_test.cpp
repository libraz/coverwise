#include <gtest/gtest.h>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "core/coverage_engine.h"
#include "core/generator.h"
#include "validator/coverage_validator.h"

using coverwise::core::CoverageEngine;
using coverwise::core::Generate;
using coverwise::model::GenerateOptions;
using coverwise::model::GenerateResult;
using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::validator::ValidateCoverage;

namespace {

/// @brief Create a uniform parameter set with identical value counts.
/// @param count Number of parameters (named "P0", "P1", ...).
/// @param values_per_param Number of values per parameter (named "v0", "v1", ...).
std::vector<Parameter> MakeUniformParams(uint32_t count, uint32_t values_per_param) {
  std::vector<Parameter> params;
  params.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    std::vector<std::string> values;
    values.reserve(values_per_param);
    for (uint32_t j = 0; j < values_per_param; ++j) {
      values.push_back("v" + std::to_string(j));
    }
    params.emplace_back("P" + std::to_string(i), std::move(values));
  }
  return params;
}

/// @brief Validate all properties of a benchmark generation result.
///
/// Checks coverage completeness, test count bounds, structural validity,
/// duplicate absence, and cross-validates with the independent coverage validator.
void ValidateBenchmarkResult(const GenerateOptions& opts, const GenerateResult& result,
                             uint32_t expected_tuples, uint32_t min_tests, uint32_t max_tests) {
  // 1. Coverage completeness via generator stats
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_EQ(result.stats.total_tuples, expected_tuples);
  EXPECT_EQ(result.stats.covered_tuples, expected_tuples);

  // 2. Test count bounds
  EXPECT_GE(result.tests.size(), min_tests)
      << "Too few tests: expected at least " << min_tests;
  EXPECT_LE(result.tests.size(), max_tests)
      << "Too many tests: expected at most " << max_tests;

  // 3. Structural validity: every test case has all params, values in range
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    ASSERT_EQ(result.tests[ti].values.size(), opts.parameters.size())
        << "Test case " << ti << " has wrong number of values";
    for (uint32_t pi = 0; pi < opts.parameters.size(); ++pi) {
      EXPECT_LT(result.tests[ti].values[pi], opts.parameters[pi].values.size())
          << "Test case " << ti << ", param " << pi << " has out-of-range value";
    }
  }

  // 4. No duplicate test cases
  std::set<std::vector<uint32_t>> seen;
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    EXPECT_TRUE(seen.insert(result.tests[ti].values).second)
        << "Duplicate test case at index " << ti;
  }

  // 5. Independent validator cross-check
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_EQ(report.total_tuples, expected_tuples);
  EXPECT_EQ(report.covered_tuples, expected_tuples);
}

}  // namespace

// --- Uniform parameter benchmarks ---

TEST(BenchmarkTest, Uniform_5x3) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(5, 3);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(5,2) * 3^2 = 10 * 9 = 90
  ValidateBenchmarkResult(opts, result, 90, 9, 18);
}

TEST(BenchmarkTest, Uniform_10x3) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(10, 3);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(10,2) * 3^2 = 45 * 9 = 405
  ValidateBenchmarkResult(opts, result, 405, 9, 25);
}

TEST(BenchmarkTest, Uniform_13x3) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(13, 3);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(13,2) * 3^2 = 78 * 9 = 702
  ValidateBenchmarkResult(opts, result, 702, 9, 30);
}

TEST(BenchmarkTest, Uniform_10x5) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(10, 5);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(10,2) * 5^2 = 45 * 25 = 1125
  ValidateBenchmarkResult(opts, result, 1125, 25, 55);
}

TEST(BenchmarkTest, Uniform_20x2) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(20, 2);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(20,2) * 2^2 = 190 * 4 = 760
  ValidateBenchmarkResult(opts, result, 760, 4, 12);
}

TEST(BenchmarkTest, Uniform_15x4) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(15, 4);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(15,2) * 4^2 = 105 * 16 = 1680
  ValidateBenchmarkResult(opts, result, 1680, 16, 44);
}

// --- Mixed parameter benchmarks ---

TEST(BenchmarkTest, Mixed_3pow4_2pow3) {
  GenerateOptions opts;
  // 4 params with 3 values + 3 params with 2 values
  for (uint32_t i = 0; i < 4; ++i) {
    opts.parameters.emplace_back("A" + std::to_string(i), std::vector<std::string>{"v0", "v1", "v2"});
  }
  for (uint32_t i = 0; i < 3; ++i) {
    opts.parameters.emplace_back("B" + std::to_string(i), std::vector<std::string>{"v0", "v1"});
  }
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // C(4,2)*3*3 + C(3,2)*2*2 + 4*3*3*2 = 6*9 + 3*4 + 12*6 = 54 + 12 + 72 = 138
  // (pairs within 3-val group) + (pairs within 2-val group) + (cross-group pairs)
  ValidateBenchmarkResult(opts, result, 138, 9, 25);
}

TEST(BenchmarkTest, Mixed_5_333_2222) {
  GenerateOptions opts;
  // 1 param with 5 values
  opts.parameters.emplace_back("X0", std::vector<std::string>{"v0", "v1", "v2", "v3", "v4"});
  // 3 params with 3 values
  for (uint32_t i = 0; i < 3; ++i) {
    opts.parameters.emplace_back("A" + std::to_string(i), std::vector<std::string>{"v0", "v1", "v2"});
  }
  // 4 params with 2 values
  for (uint32_t i = 0; i < 4; ++i) {
    opts.parameters.emplace_back("B" + std::to_string(i), std::vector<std::string>{"v0", "v1"});
  }
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  // Pairs: 5-5: 0, 5-3: 1*3=3 pairs * 5*3=15 each = 45, 5-2: 1*4=4 pairs * 5*2=10 each = 40,
  //        3-3: C(3,2)=3 pairs * 3*3=9 each = 27, 3-2: 3*4=12 pairs * 3*2=6 each = 72,
  //        2-2: C(4,2)=6 pairs * 2*2=4 each = 24
  // Total: 45 + 40 + 27 + 72 + 24 = 208
  ValidateBenchmarkResult(opts, result, 208, 15, 50);
}

// --- Coverage monotonicity test ---

TEST(BenchmarkTest, CoverageMonotonicity_10x3) {
  GenerateOptions opts;
  opts.parameters = MakeUniformParams(10, 3);
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);
  ASSERT_FALSE(result.tests.empty());

  // Replay test cases through a fresh CoverageEngine, verifying that each test case
  // adds at least one new covered tuple (monotonically increasing coverage).
  auto [engine, err] = CoverageEngine::Create(opts.parameters, opts.strength);
  ASSERT_TRUE(err.message.empty()) << "CoverageEngine creation failed: " << err.message;

  uint32_t prev_covered = 0;
  for (size_t i = 0; i < result.tests.size(); ++i) {
    uint32_t score = engine.ScoreCandidate(result.tests[i]);
    engine.AddTestCase(result.tests[i]);
    uint32_t current_covered = engine.CoveredCount();

    // Each test case must contribute at least one new tuple
    EXPECT_GT(current_covered, prev_covered)
        << "Test case " << i << " did not increase coverage (score=" << score << ")";
    EXPECT_EQ(current_covered, prev_covered + score)
        << "Coverage increase mismatch at test case " << i;

    prev_covered = current_covered;
  }

  // After all test cases, coverage must be complete
  EXPECT_TRUE(engine.IsComplete());
  EXPECT_EQ(engine.CoveredCount(), 405u);
}
