#include <gtest/gtest.h>

#include "core/generator.h"
#include "model/constraint_parser.h"
#include "validator/constraint_validator.h"
#include "validator/coverage_validator.h"

using coverwise::core::Generate;
using coverwise::core::GenerateOptions;
using coverwise::model::Constraint;
using coverwise::model::Parameter;
using coverwise::model::ParseConstraint;
using coverwise::model::TestCase;
using coverwise::validator::ValidateConstraints;
using coverwise::validator::ValidateCoverage;

TEST(IntegrationTest, GenerateAndValidatePairwise) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
      {"C", {"0", "1"}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Validate with independent validator
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_EQ(report.total_tuples, 12u);  // C(3,2) * 2^2 = 3 * 4 = 12
  EXPECT_EQ(report.covered_tuples, 12u);
}

TEST(IntegrationTest, DeterministicOutput) {
  GenerateOptions opts;
  opts.parameters = {
      {"X", {"a", "b", "c"}},
      {"Y", {"1", "2"}},
      {"Z", {"p", "q"}},
  };
  opts.strength = 2;
  opts.seed = 12345;

  auto result1 = Generate(opts);
  auto result2 = Generate(opts);

  ASSERT_EQ(result1.tests.size(), result2.tests.size());
  for (size_t i = 0; i < result1.tests.size(); ++i) {
    EXPECT_EQ(result1.tests[i].values, result2.tests[i].values);
  }
}

TEST(IntegrationTest, ThreeByThreePairwise) {
  GenerateOptions opts;
  opts.parameters = {
      {"P1", {"a", "b", "c"}},
      {"P2", {"x", "y", "z"}},
      {"P3", {"1", "2", "3"}},
  };
  opts.strength = 2;
  opts.seed = 99;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // C(3,2) = 3 combinations, each 3*3 = 9 tuples, total = 27
  EXPECT_EQ(report.total_tuples, 27u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_EQ(result.coverage, 1.0);
}

TEST(IntegrationTest, FourParamsMixed) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}},
      {"B", {"b0", "b1", "b2"}},
      {"C", {"c0", "c1", "c2", "c3"}},
      {"D", {"d0", "d1"}},
  };
  opts.strength = 2;
  opts.seed = 777;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // C(4,2) = 6 combinations.
  // (A,B): 2*3=6, (A,C): 2*4=8, (A,D): 2*2=4, (B,C): 3*4=12,
  // (B,D): 3*2=6, (C,D): 4*2=8. Total = 6+8+4+12+6+8 = 44
  EXPECT_EQ(report.total_tuples, 44u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
}

TEST(IntegrationTest, MaxTestsLimitation) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "a2", "a3"}},
      {"B", {"b0", "b1", "b2", "b3"}},
      {"C", {"c0", "c1", "c2", "c3"}},
  };
  opts.strength = 2;
  opts.seed = 42;
  opts.max_tests = 3;  // 3 tests cannot cover all 48 tuples

  auto result = Generate(opts);

  EXPECT_LE(result.tests.size(), 3u);
  EXPECT_LT(result.coverage, 1.0);
  EXPECT_FALSE(result.uncovered.empty());
  EXPECT_GT(result.stats.total_tuples, result.stats.covered_tuples);

  // Also validate with independent validator
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_LT(report.coverage_ratio, 1.0);
  EXPECT_FALSE(report.uncovered.empty());
}

TEST(IntegrationTest, SeedTestPreloading) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
      {"C", {"0", "1"}},
  };
  opts.strength = 2;
  opts.seed = 42;

  // Provide two seed tests.
  TestCase seed1;
  seed1.values = {0, 0, 0};  // A=0, B=0, C=0
  TestCase seed2;
  seed2.values = {1, 1, 1};  // A=1, B=1, C=1
  opts.seeds = {seed1, seed2};

  auto result = Generate(opts);

  // Seed tests must appear at the front of the output.
  ASSERT_GE(result.tests.size(), 2u);
  EXPECT_EQ(result.tests[0].values, seed1.values);
  EXPECT_EQ(result.tests[1].values, seed2.values);

  // Coverage must still be 100%.
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
}

TEST(IntegrationTest, ThreeWiseGeneration) {
  GenerateOptions opts;
  opts.parameters = {
      {"P1", {"a", "b", "c"}},
      {"P2", {"x", "y", "z"}},
      {"P3", {"1", "2", "3"}},
      {"P4", {"p", "q", "r"}},
  };
  opts.strength = 3;
  opts.seed = 42;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // C(4,3) = 4 combinations, each 3^3 = 27 tuples, total = 108
  EXPECT_EQ(report.total_tuples, 108u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_EQ(report.covered_tuples, 108u);
}

TEST(IntegrationTest, FourWiseGeneration) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
      {"C", {"0", "1"}},
      {"D", {"0", "1"}},
  };
  opts.strength = 4;
  opts.seed = 42;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // C(4,4) = 1 combination, 2^4 = 16 tuples
  EXPECT_EQ(report.total_tuples, 16u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_EQ(report.covered_tuples, 16u);
}

TEST(IntegrationTest, StrengthOneSingleParam) {
  GenerateOptions opts;
  opts.parameters = {
      {"Color", {"red", "green", "blue", "yellow", "purple"}},
  };
  opts.strength = 1;
  opts.seed = 42;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // C(1,1) = 1 combination, 5 values => 5 tuples
  EXPECT_EQ(report.total_tuples, 5u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_GE(result.tests.size(), 5u);
}

TEST(IntegrationTest, StrengthEqualsParamCount) {
  GenerateOptions opts;
  opts.parameters = {
      {"X", {"0", "1"}},
      {"Y", {"0", "1"}},
      {"Z", {"0", "1"}},
  };
  opts.strength = 3;
  opts.seed = 42;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // C(3,3) = 1 combination, 2^3 = 8 tuples (exhaustive)
  EXPECT_EQ(report.total_tuples, 8u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
  EXPECT_EQ(report.covered_tuples, 8u);
  // Exhaustive coverage requires at least 8 tests
  EXPECT_GE(result.tests.size(), 8u);
}

TEST(IntegrationTest, ValidatorIndependence) {
  GenerateOptions opts;
  opts.parameters = {
      {"OS", {"win", "mac", "linux"}},
      {"Browser", {"chrome", "firefox"}},
      {"Lang", {"en", "ja"}},
  };
  opts.strength = 2;
  opts.seed = 555;

  auto result = Generate(opts);

  // Generator's own coverage ratio.
  double generator_coverage = result.coverage;

  // Independent validator's coverage ratio.
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  // Both must agree.
  EXPECT_DOUBLE_EQ(generator_coverage, report.coverage_ratio);
  EXPECT_EQ(result.stats.total_tuples, report.total_tuples);
  EXPECT_EQ(result.stats.covered_tuples, report.covered_tuples);

  // Both must report 100% for this configuration.
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
}

// ---------------------------------------------------------------------------
// Constraint integration tests
// ---------------------------------------------------------------------------

/// @brief Helper to parse constraints into AST for validation.
static std::vector<Constraint> ParseConstraints(const std::vector<std::string>& expressions,
                                                const std::vector<Parameter>& params) {
  std::vector<Constraint> constraints;
  for (const auto& expr : expressions) {
    auto result = ParseConstraint(expr, params);
    EXPECT_TRUE(result.error.ok()) << result.error.message;
    constraints.push_back(std::move(result.constraint));
  }
  return constraints;
}

TEST(IntegrationTest, ConstraintIfThen) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}},
      {"browser", {"chrome", "firefox", "safari", "ie"}},
  };
  opts.constraint_expressions = {"IF os=mac THEN browser!=ie"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // No warnings means constraints parsed successfully.
  EXPECT_TRUE(result.warnings.empty());

  // 100% coverage of valid tuples.
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate no constraint violations.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify no test has os=mac AND browser=ie.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);  // "mac" is index 1
    bool is_ie = (tc.values[1] == 3);   // "ie" is index 3
    EXPECT_FALSE(is_mac && is_ie) << "Found os=mac, browser=ie violation";
  }
}

TEST(IntegrationTest, ConstraintNot) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac"}},
      {"browser", {"chrome", "safari"}},
  };
  opts.constraint_expressions = {"NOT (os=win AND browser=safari)"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate no constraint violations.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify no test has os=win AND browser=safari.
  for (const auto& tc : result.tests) {
    bool is_win = (tc.values[0] == 0);     // "win" is index 0
    bool is_safari = (tc.values[1] == 1);  // "safari" is index 1
    EXPECT_FALSE(is_win && is_safari) << "Found os=win, browser=safari violation";
  }
}

TEST(IntegrationTest, MultipleConstraints) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}},
      {"browser", {"chrome", "firefox", "ie"}},
      {"arch", {"x64", "arm"}},
  };
  opts.constraint_expressions = {
      "IF os=mac THEN browser!=ie",
      "IF os=linux THEN arch!=arm",
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate no constraint violations.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify specific violations are absent.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);
    bool is_linux = (tc.values[0] == 2);
    bool is_ie = (tc.values[1] == 2);   // "ie" is index 2
    bool is_arm = (tc.values[2] == 1);  // "arm" is index 1
    EXPECT_FALSE(is_mac && is_ie) << "Found os=mac, browser=ie violation";
    EXPECT_FALSE(is_linux && is_arm) << "Found os=linux, arch=arm violation";
  }
}

TEST(IntegrationTest, ConstraintIfThenElse) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}},
      {"browser", {"chrome", "firefox", "safari", "ie"}},
      {"arch", {"x64", "arm", "riscv"}},
  };
  opts.constraint_expressions = {"IF os=mac THEN browser!=ie ELSE arch!=arm"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate no constraint violations via the independent validator.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Manually verify: os=mac => browser!=ie, os!=mac => arch!=arm.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);
    bool is_ie = (tc.values[1] == 3);
    bool is_arm = (tc.values[2] == 1);
    if (is_mac) {
      EXPECT_FALSE(is_ie) << "Found os=mac, browser=ie (then-branch violation)";
    } else {
      EXPECT_FALSE(is_arm) << "Found os!=mac, arch=arm (else-branch violation)";
    }
  }
}

TEST(IntegrationTest, ConstraintRelational) {
  GenerateOptions opts;
  opts.parameters = {
      {"priority", {"1", "2", "3", "4", "5"}},
      {"env", {"dev", "staging", "prod"}},
      {"region", {"us", "eu", "ap"}},
  };
  opts.constraint_expressions = {"priority > 2"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify: every test case has priority > 2, i.e. value "3", "4", or "5" (indices 2,3,4).
  for (const auto& tc : result.tests) {
    uint32_t pval = tc.values[0];
    EXPECT_GE(pval, 2u) << "priority value index " << pval << " corresponds to value <= 2";
  }
}

TEST(IntegrationTest, ConstraintIn) {
  GenerateOptions opts;
  opts.parameters = {
      {"env", {"dev", "staging", "prod"}},
      {"region", {"us", "eu", "ap"}},
      {"tier", {"free", "pro", "enterprise"}},
  };
  opts.constraint_expressions = {"env IN {staging, prod}"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify: every test case has env IN {staging, prod} (indices 1 or 2, not 0=dev).
  for (const auto& tc : result.tests) {
    EXPECT_NE(tc.values[0], 0u) << "Found env=dev, violating IN {staging, prod}";
  }
}

TEST(IntegrationTest, ConstraintLike) {
  GenerateOptions opts;
  opts.parameters = {
      {"browser", {"chrome", "chrome-beta", "firefox", "safari"}},
      {"os", {"win", "mac", "linux"}},
  };
  opts.constraint_expressions = {"browser LIKE chrome*"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify: every test case has browser matching chrome*. Only indices 0, 1 match.
  for (const auto& tc : result.tests) {
    EXPECT_LE(tc.values[0], 1u) << "Found browser that does not match chrome*";
  }
}

TEST(IntegrationTest, ConstraintParamComparison) {
  // Use param-to-param != which is simpler for the generator to handle
  // than relational comparisons (< requires numeric ordering awareness).
  GenerateOptions opts;
  opts.parameters = {
      {"src", {"a", "b", "c"}},
      {"dst", {"a", "b", "c"}},
      {"mode", {"fast", "slow"}},
  };
  opts.constraint_expressions = {"src != dst"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify: for every test case, src != dst (string values differ).
  for (const auto& tc : result.tests) {
    // Values at same index have the same string ("a","b","c"), so same index
    // means same value which would violate the constraint.
    EXPECT_NE(tc.values[0], tc.values[1])
        << "src and dst have same value index " << tc.values[0];
  }
}

TEST(IntegrationTest, UnconditionalConstraint) {
  GenerateOptions opts;
  opts.parameters = {
      {"priority", {"1", "2", "3", "4", "5"}},
      {"env", {"dev", "staging", "prod"}},
  };
  // Standalone constraint (no IF): acts as invariant for every test case.
  opts.constraint_expressions = {"priority > 3"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify: every test case has priority > 3, i.e. value "4" or "5" (indices 3 or 4).
  for (const auto& tc : result.tests) {
    EXPECT_GE(tc.values[0], 3u)
        << "priority index " << tc.values[0] << " corresponds to value <= 3";
  }
}

TEST(IntegrationTest, ConstraintReducesTuples) {
  // Without constraints: total tuples for pairwise of 3x4 = 12.
  // With constraint "IF os=mac THEN browser!=ie", the tuple (os=mac, browser=ie)
  // is invalid and excluded.

  GenerateOptions unconstrained;
  unconstrained.parameters = {
      {"os", {"win", "mac", "linux"}},
      {"browser", {"chrome", "firefox", "safari", "ie"}},
  };
  unconstrained.strength = 2;
  unconstrained.seed = 42;

  auto result_unconstrained = Generate(unconstrained);

  GenerateOptions constrained;
  constrained.parameters = unconstrained.parameters;
  constrained.constraint_expressions = {"IF os=mac THEN browser!=ie"};
  constrained.strength = 2;
  constrained.seed = 42;

  auto result_constrained = Generate(constrained);

  // Constrained should have fewer total tuples.
  EXPECT_LT(result_constrained.stats.total_tuples, result_unconstrained.stats.total_tuples);

  // Both should achieve 100% coverage.
  EXPECT_DOUBLE_EQ(result_unconstrained.coverage, 1.0);
  EXPECT_DOUBLE_EQ(result_constrained.coverage, 1.0);
}

// ===========================================================================
// Edge case integration tests
// ===========================================================================

TEST(IntegrationEdgeCaseTest, SingleValueParam) {
  GenerateOptions opts;
  opts.parameters = {
      {"fixed", {"only"}},
      {"mode", {"a", "b", "c"}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // The single-value parameter must appear as "only" (index 0) in every test case.
  for (const auto& tc : result.tests) {
    EXPECT_EQ(tc.values[0], 0u) << "Single-value param 'fixed' should always be index 0";
  }

  // Coverage must still be 100%.
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
}

TEST(IntegrationEdgeCaseTest, MultibyteConstraintGeneration) {
  GenerateOptions opts;
  opts.parameters = {
      {"OS", {"Windows", "macOS", "Linux"}},
      {"\xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6",  // "ブラウザ"
       {"Chrome", "Safari", "Firefox"}},
  };
  // IF OS=macOS THEN ブラウザ!=Safari (just testing parse + generation)
  opts.constraint_expressions = {
      "IF OS=macOS THEN "
      "\xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6!=Safari"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate no constraint violations.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Manually verify: OS=macOS => ブラウザ!=Safari.
  for (const auto& tc : result.tests) {
    bool is_macos = (tc.values[0] == 1);   // "macOS" is index 1
    bool is_safari = (tc.values[1] == 1);  // "Safari" is index 1
    EXPECT_FALSE(is_macos && is_safari)
        << "Found OS=macOS, browser=Safari violation";
  }
}

TEST(IntegrationEdgeCaseTest, NegativeNumberConstraint) {
  GenerateOptions opts;
  opts.parameters = {
      {"temp", {"-10", "-1", "0", "1", "10"}},
      {"unit", {"celsius", "fahrenheit"}},
  };
  opts.constraint_expressions = {"temp >= -1"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate no constraint violations.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify: every test has temp >= -1, meaning values "-1", "0", "1", "10"
  // (indices 1, 2, 3, 4). Index 0 ("-10") should never appear.
  for (const auto& tc : result.tests) {
    EXPECT_GE(tc.values[0], 1u)
        << "temp index " << tc.values[0] << " corresponds to value < -1";
  }
}
