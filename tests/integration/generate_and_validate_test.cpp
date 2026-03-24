#include <gtest/gtest.h>

#include "core/generator.h"
#include "model/constraint_parser.h"
#include "validator/constraint_validator.h"
#include "validator/coverage_validator.h"

using coverwise::core::Generate;
using coverwise::model::Constraint;
using coverwise::model::GenerateOptions;
using coverwise::model::Parameter;
using coverwise::model::ParseConstraint;
using coverwise::model::TestCase;
using coverwise::validator::ComputeClassCoverage;
using coverwise::validator::ValidateConstraints;
using coverwise::validator::ValidateCoverage;

namespace {

/// @brief Helper to create a parameter with equivalence classes.
Parameter MakeClassParam(const std::string& name, std::vector<std::string> values,
                         std::vector<std::string> classes) {
  Parameter p(name, std::move(values));
  p.set_equivalence_classes(std::move(classes));
  return p;
}

}  // namespace

TEST(IntegrationTest, GenerateAndValidatePairwise) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
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
      {"X", {"a", "b", "c"}, {}},
      {"Y", {"1", "2"}, {}},
      {"Z", {"p", "q"}, {}},
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
      {"P1", {"a", "b", "c"}, {}},
      {"P2", {"x", "y", "z"}, {}},
      {"P3", {"1", "2", "3"}, {}},
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
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1", "b2"}, {}},
      {"C", {"c0", "c1", "c2", "c3"}, {}},
      {"D", {"d0", "d1"}, {}},
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
      {"A", {"a0", "a1", "a2", "a3"}, {}},
      {"B", {"b0", "b1", "b2", "b3"}, {}},
      {"C", {"c0", "c1", "c2", "c3"}, {}},
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
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
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
      {"P1", {"a", "b", "c"}, {}},
      {"P2", {"x", "y", "z"}, {}},
      {"P3", {"1", "2", "3"}, {}},
      {"P4", {"p", "q", "r"}, {}},
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
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
      {"D", {"0", "1"}, {}},
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
      {"Color", {"red", "green", "blue", "yellow", "purple"}, {}},
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
      {"X", {"0", "1"}, {}},
      {"Y", {"0", "1"}, {}},
      {"Z", {"0", "1"}, {}},
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
      {"OS", {"win", "mac", "linux"}, {}},
      {"Browser", {"chrome", "firefox"}, {}},
      {"Lang", {"en", "ja"}, {}},
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
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari", "ie"}, {}},
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
      {"os", {"win", "mac"}, {}},
      {"browser", {"chrome", "safari"}, {}},
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
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "ie"}, {}},
      {"arch", {"x64", "arm"}, {}},
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
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari", "ie"}, {}},
      {"arch", {"x64", "arm", "riscv"}, {}},
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
      {"priority", {"1", "2", "3", "4", "5"}, {}},
      {"env", {"dev", "staging", "prod"}, {}},
      {"region", {"us", "eu", "ap"}, {}},
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
      {"env", {"dev", "staging", "prod"}, {}},
      {"region", {"us", "eu", "ap"}, {}},
      {"tier", {"free", "pro", "enterprise"}, {}},
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
      {"browser", {"chrome", "chrome-beta", "firefox", "safari"}, {}},
      {"os", {"win", "mac", "linux"}, {}},
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
      {"src", {"a", "b", "c"}, {}},
      {"dst", {"a", "b", "c"}, {}},
      {"mode", {"fast", "slow"}, {}},
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
    EXPECT_NE(tc.values[0], tc.values[1]) << "src and dst have same value index " << tc.values[0];
  }
}

TEST(IntegrationTest, UnconditionalConstraint) {
  GenerateOptions opts;
  opts.parameters = {
      {"priority", {"1", "2", "3", "4", "5"}, {}},
      {"env", {"dev", "staging", "prod"}, {}},
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
    EXPECT_GE(tc.values[0], 3u) << "priority index " << tc.values[0]
                                << " corresponds to value <= 3";
  }
}

TEST(IntegrationTest, ConstraintReducesTuples) {
  // Without constraints: total tuples for pairwise of 3x4 = 12.
  // With constraint "IF os=mac THEN browser!=ie", the tuple (os=mac, browser=ie)
  // is invalid and excluded.

  GenerateOptions unconstrained;
  unconstrained.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari", "ie"}, {}},
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
      {"fixed", {"only"}, {}},
      {"mode", {"a", "b", "c"}, {}},
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
      {"OS", {"Windows", "macOS", "Linux"}, {}},
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
    EXPECT_FALSE(is_macos && is_safari) << "Found OS=macOS, browser=Safari violation";
  }
}

TEST(IntegrationEdgeCaseTest, NegativeNumberConstraint) {
  GenerateOptions opts;
  opts.parameters = {
      {"temp", {"-10", "-1", "0", "1", "10"}, {}},
      {"unit", {"celsius", "fahrenheit"}, {}},
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
    EXPECT_GE(tc.values[0], 1u) << "temp index " << tc.values[0] << " corresponds to value < -1";
  }
}

// ===========================================================================
// Sub-model (mixed strength) integration tests
// ===========================================================================

using coverwise::model::SubModel;

TEST(IntegrationSubModelTest, SubModelHigherStrength) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari"}, {}},
      {"arch", {"x64", "arm"}, {}},
      {"lang", {"en", "ja"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  // Require 3-wise coverage for (os, browser, arch).
  SubModel sm;
  sm.parameter_names = {"os", "browser", "arch"};
  sm.strength = 3;
  opts.sub_models = {sm};

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate global pairwise coverage.
  auto report2 = ValidateCoverage(opts.parameters, result.tests, 2);
  EXPECT_DOUBLE_EQ(report2.coverage_ratio, 1.0);

  // Validate 3-wise coverage for the sub-model parameters only.
  std::vector<Parameter> sub_params = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari"}, {}},
      {"arch", {"x64", "arm"}, {}},
  };
  // Project test cases to sub-model parameters (indices 0, 1, 2 of original).
  std::vector<TestCase> projected;
  for (const auto& tc : result.tests) {
    TestCase ptc;
    ptc.values = {tc.values[0], tc.values[1], tc.values[2]};
    projected.push_back(ptc);
  }
  auto report3 = ValidateCoverage(sub_params, projected, 3);
  // C(3,3) = 1 combination, 3*3*2 = 18 tuples
  EXPECT_EQ(report3.total_tuples, 18u);
  EXPECT_DOUBLE_EQ(report3.coverage_ratio, 1.0);
  EXPECT_EQ(report3.uncovered.size(), 0u);
}

TEST(IntegrationSubModelTest, SubModelMultiple) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "a2"}, {}}, {"B", {"b0", "b1", "b2"}, {}}, {"C", {"c0", "c1"}, {}},
      {"D", {"d0", "d1"}, {}},       {"E", {"e0", "e1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 99;

  // Two sub-models with different strengths.
  SubModel sm1;
  sm1.parameter_names = {"A", "B", "C"};
  sm1.strength = 3;

  SubModel sm2;
  sm2.parameter_names = {"C", "D", "E"};
  sm2.strength = 3;

  opts.sub_models = {sm1, sm2};

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate global pairwise.
  auto report2 = ValidateCoverage(opts.parameters, result.tests, 2);
  EXPECT_DOUBLE_EQ(report2.coverage_ratio, 1.0);

  // Validate 3-wise for sub-model 1: A, B, C (indices 0, 1, 2).
  {
    std::vector<Parameter> sp = {
        {"A", {"a0", "a1", "a2"}, {}},
        {"B", {"b0", "b1", "b2"}, {}},
        {"C", {"c0", "c1"}, {}},
    };
    std::vector<TestCase> proj;
    for (const auto& tc : result.tests) {
      TestCase ptc;
      ptc.values = {tc.values[0], tc.values[1], tc.values[2]};
      proj.push_back(ptc);
    }
    auto r = ValidateCoverage(sp, proj, 3);
    EXPECT_DOUBLE_EQ(r.coverage_ratio, 1.0) << "Sub-model 1 (A,B,C) not fully 3-wise covered";
  }

  // Validate 3-wise for sub-model 2: C, D, E (indices 2, 3, 4).
  {
    std::vector<Parameter> sp = {
        {"C", {"c0", "c1"}, {}},
        {"D", {"d0", "d1"}, {}},
        {"E", {"e0", "e1"}, {}},
    };
    std::vector<TestCase> proj;
    for (const auto& tc : result.tests) {
      TestCase ptc;
      ptc.values = {tc.values[2], tc.values[3], tc.values[4]};
      proj.push_back(ptc);
    }
    auto r = ValidateCoverage(sp, proj, 3);
    EXPECT_DOUBLE_EQ(r.coverage_ratio, 1.0) << "Sub-model 2 (C,D,E) not fully 3-wise covered";
  }
}

TEST(IntegrationSubModelTest, SubModelWithConstraints) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "ie"}, {}},
      {"arch", {"x64", "arm"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;
  opts.constraint_expressions = {"IF os=mac THEN browser!=ie"};

  SubModel sm;
  sm.parameter_names = {"os", "browser", "arch"};
  sm.strength = 3;
  opts.sub_models = {sm};

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Verify no constraint violations.
  auto constraints = ParseConstraints(opts.constraint_expressions, opts.parameters);
  auto creport = ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(creport.violations, 0u);

  // Verify no test has os=mac AND browser=ie.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);
    bool is_ie = (tc.values[1] == 2);
    EXPECT_FALSE(is_mac && is_ie) << "Found os=mac, browser=ie violation";
  }
}

TEST(IntegrationSubModelTest, SubModelOverlapping) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
      {"D", {"d0", "d1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  // Parameter B appears in both sub-models.
  SubModel sm1;
  sm1.parameter_names = {"A", "B", "C"};
  sm1.strength = 3;

  SubModel sm2;
  sm2.parameter_names = {"B", "C", "D"};
  sm2.strength = 3;

  opts.sub_models = {sm1, sm2};

  auto result = Generate(opts);

  EXPECT_TRUE(result.warnings.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Validate global pairwise.
  auto report2 = ValidateCoverage(opts.parameters, result.tests, 2);
  EXPECT_DOUBLE_EQ(report2.coverage_ratio, 1.0);

  // Validate 3-wise for sub-model 1: A, B, C (indices 0, 1, 2).
  {
    std::vector<Parameter> sp = {
        {"A", {"a0", "a1"}, {}},
        {"B", {"b0", "b1"}, {}},
        {"C", {"c0", "c1"}, {}},
    };
    std::vector<TestCase> proj;
    for (const auto& tc : result.tests) {
      TestCase ptc;
      ptc.values = {tc.values[0], tc.values[1], tc.values[2]};
      proj.push_back(ptc);
    }
    auto r = ValidateCoverage(sp, proj, 3);
    EXPECT_DOUBLE_EQ(r.coverage_ratio, 1.0) << "Sub-model 1 (A,B,C) not fully 3-wise covered";
  }

  // Validate 3-wise for sub-model 2: B, C, D (indices 1, 2, 3).
  {
    std::vector<Parameter> sp = {
        {"B", {"b0", "b1"}, {}},
        {"C", {"c0", "c1"}, {}},
        {"D", {"d0", "d1"}, {}},
    };
    std::vector<TestCase> proj;
    for (const auto& tc : result.tests) {
      TestCase ptc;
      ptc.values = {tc.values[1], tc.values[2], tc.values[3]};
      proj.push_back(ptc);
    }
    auto r = ValidateCoverage(sp, proj, 3);
    EXPECT_DOUBLE_EQ(r.coverage_ratio, 1.0) << "Sub-model 2 (B,C,D) not fully 3-wise covered";
  }
}

TEST(IntegrationSubModelTest, NoSubModelsBackwardCompatible) {
  // Ensure that not using sub_models produces the same result as before.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;
  // sub_models left empty.

  auto result = Generate(opts);

  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.total_tuples, 12u);
  EXPECT_EQ(report.uncovered.size(), 0u);
}

// ===========================================================================
// Negative testing (invalid value) integration tests
// ===========================================================================

TEST(IntegrationNegativeTest, BasicNegativeGeneration) {
  // One parameter with one invalid value.
  GenerateOptions opts;
  opts.parameters = {
      {"browser", {"chrome", "safari", "ie6"}, {false, false, true}},
      {"os", {"win", "mac"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Positive tests should have 100% coverage of valid tuples.
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.warnings.empty());

  // Positive tests must not contain invalid values.
  for (const auto& tc : result.tests) {
    EXPECT_FALSE(opts.parameters[0].is_invalid(tc.values[0]))
        << "Positive test contains invalid value ie6";
  }

  // Negative tests should exist.
  EXPECT_FALSE(result.negative_tests.empty());

  // Every negative test must have exactly one invalid value.
  for (const auto& tc : result.negative_tests) {
    uint32_t invalid_count = 0;
    for (uint32_t pi = 0; pi < static_cast<uint32_t>(opts.parameters.size()); ++pi) {
      if (opts.parameters[pi].is_invalid(tc.values[pi])) {
        ++invalid_count;
      }
    }
    EXPECT_EQ(invalid_count, 1u) << "Negative test must have exactly 1 invalid value";
  }
}

TEST(IntegrationNegativeTest, OneInvalidPerTest) {
  // Multiple parameters with invalid values.
  GenerateOptions opts;
  opts.parameters = {
      {"browser", {"chrome", "safari", "ie6"}, {false, false, true}},
      {"os", {"win", "mac", "dos"}, {false, false, true}},
      {"lang", {"en", "ja"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Positive tests: no invalid values at all.
  for (const auto& tc : result.tests) {
    for (uint32_t pi = 0; pi < static_cast<uint32_t>(opts.parameters.size()); ++pi) {
      EXPECT_FALSE(opts.parameters[pi].is_invalid(tc.values[pi]))
          << "Positive test contains invalid value at param " << opts.parameters[pi].name;
    }
  }

  // Negative tests: exactly 1 invalid value per test.
  EXPECT_FALSE(result.negative_tests.empty());
  for (const auto& tc : result.negative_tests) {
    uint32_t invalid_count = 0;
    for (uint32_t pi = 0; pi < static_cast<uint32_t>(opts.parameters.size()); ++pi) {
      if (opts.parameters[pi].is_invalid(tc.values[pi])) {
        ++invalid_count;
      }
    }
    EXPECT_EQ(invalid_count, 1u) << "Negative test must have exactly 1 invalid value";
  }
}

TEST(IntegrationNegativeTest, NegativeTestCoverage) {
  // Verify that invalid values are paired with all valid values of other params.
  GenerateOptions opts;
  opts.parameters = {
      {"browser", {"chrome", "safari", "ie6"}, {false, false, true}},
      {"os", {"win", "mac"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Collect which os values appear with ie6 in negative tests.
  std::vector<bool> os_seen(2, false);
  for (const auto& tc : result.negative_tests) {
    if (tc.values[0] == 2) {  // browser=ie6
      os_seen[tc.values[1]] = true;
    }
  }

  // Both os=win and os=mac should appear with browser=ie6.
  EXPECT_TRUE(os_seen[0]) << "Missing pairing: browser=ie6, os=win";
  EXPECT_TRUE(os_seen[1]) << "Missing pairing: browser=ie6, os=mac";
}

TEST(IntegrationNegativeTest, PositiveSeparation) {
  // Verify positive tests contain zero invalid values.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "a_bad"}, {false, false, true}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c_bad", "c1"}, {false, true, false}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // No positive test should have any invalid value.
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    const auto& tc = result.tests[ti];
    for (uint32_t pi = 0; pi < static_cast<uint32_t>(opts.parameters.size()); ++pi) {
      EXPECT_FALSE(opts.parameters[pi].is_invalid(tc.values[pi]))
          << "Positive test " << ti << " contains invalid value for param "
          << opts.parameters[pi].name;
    }
  }

  // Negative tests should exist for both a_bad and c_bad.
  bool has_a_bad = false;
  bool has_c_bad = false;
  for (const auto& tc : result.negative_tests) {
    if (tc.values[0] == 2) has_a_bad = true;  // A=a_bad
    if (tc.values[2] == 1) has_c_bad = true;  // C=c_bad
  }
  EXPECT_TRUE(has_a_bad) << "No negative test for A=a_bad";
  EXPECT_TRUE(has_c_bad) << "No negative test for C=c_bad";
}

TEST(IntegrationNegativeTest, NoInvalidValuesNoNegativeTests) {
  // When no values are marked invalid, negative_tests should be empty.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.negative_tests.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
}

TEST(IntegrationNegativeTest, NegativeTestDeterministic) {
  // Same seed produces identical negative tests.
  GenerateOptions opts;
  opts.parameters = {
      {"browser", {"chrome", "ie6"}, {false, true}},
      {"os", {"win", "mac", "linux"}, {}},
  };
  opts.strength = 2;
  opts.seed = 12345;

  auto result1 = Generate(opts);
  auto result2 = Generate(opts);

  ASSERT_EQ(result1.negative_tests.size(), result2.negative_tests.size());
  for (size_t i = 0; i < result1.negative_tests.size(); ++i) {
    EXPECT_EQ(result1.negative_tests[i].values, result2.negative_tests[i].values);
  }
}

// ---------------------------------------------------------------------------
// Aliasing tests
// ---------------------------------------------------------------------------

TEST(IntegrationTest, AliasingBasic) {
  // "chromium" has aliases "chrome", "edge", "brave".
  // Coverage should treat it as 1 value (3 total for browser).
  GenerateOptions opts;
  Parameter browser("browser", {"chromium", "firefox", "safari"});
  browser.set_aliases({{"chrome", "edge", "brave"}, {}, {}});
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      browser,
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Validate coverage uses primary values only: 3 * 3 = 9 tuples.
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.total_tuples, 9u);
  EXPECT_EQ(report.uncovered.size(), 0u);

  // Verify display_name rotation works for chromium (index 0).
  // With 4 names (chromium + 3 aliases), rotation should cycle.
  EXPECT_EQ(opts.parameters[1].display_name(0, 0), "chromium");
  EXPECT_EQ(opts.parameters[1].display_name(0, 1), "chrome");
  EXPECT_EQ(opts.parameters[1].display_name(0, 2), "edge");
  EXPECT_EQ(opts.parameters[1].display_name(0, 3), "brave");
  EXPECT_EQ(opts.parameters[1].display_name(0, 4), "chromium");  // wraps

  // Non-aliased values always return primary name.
  EXPECT_EQ(opts.parameters[1].display_name(1, 0), "firefox");
  EXPECT_EQ(opts.parameters[1].display_name(1, 5), "firefox");
}

TEST(IntegrationTest, AliasingConstraint) {
  // Constraint references an alias name "chrome" which should resolve to "chromium".
  GenerateOptions opts;
  Parameter browser("browser", {"chromium", "firefox", "safari"});
  browser.set_aliases({{"chrome", "edge"}, {}, {}});
  opts.parameters = {
      {"os", {"win", "mac"}, {}},
      browser,
  };
  opts.strength = 2;
  opts.seed = 42;
  // "chrome" is an alias for "chromium". Constraint: IF os=mac THEN browser!=chrome
  // This should exclude (os=mac, browser=chromium).
  opts.constraint_expressions = {"IF os = mac THEN browser != chrome"};

  auto result = Generate(opts);

  // Verify no test case has os=mac + browser=chromium.
  for (const auto& tc : result.tests) {
    if (tc.values[0] == 1) {  // os=mac
      EXPECT_NE(tc.values[1], 0u) << "Constraint violated: os=mac with browser=chromium (alias)";
    }
  }

  // Coverage should still be high (only the excluded tuple is missing or excluded).
  EXPECT_GE(result.coverage, 0.5);
}

TEST(IntegrationTest, AliasingNoEffect) {
  // No aliases defined — behavior should be identical to non-aliased generation.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 99;

  auto result = Generate(opts);
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);

  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.total_tuples, 4u);

  // display_name with no aliases returns primary name regardless of rotation.
  EXPECT_EQ(opts.parameters[0].display_name(0, 0), "a0");
  EXPECT_EQ(opts.parameters[0].display_name(0, 100), "a0");
  EXPECT_FALSE(opts.parameters[0].has_aliases());
  EXPECT_FALSE(opts.parameters[1].has_aliases());
}

TEST(IntegrationTest, AliasingFindValueIndex) {
  // Test that find_value_index resolves both primary values and aliases.
  Parameter browser("browser", {"chromium", "firefox", "safari"});
  browser.set_aliases({{"chrome", "edge", "brave"}, {}, {}});

  EXPECT_EQ(browser.find_value_index("chromium"), 0u);
  EXPECT_EQ(browser.find_value_index("chrome"), 0u);
  EXPECT_EQ(browser.find_value_index("edge"), 0u);
  EXPECT_EQ(browser.find_value_index("brave"), 0u);
  EXPECT_EQ(browser.find_value_index("firefox"), 1u);
  EXPECT_EQ(browser.find_value_index("safari"), 2u);
  EXPECT_EQ(browser.find_value_index("opera"), UINT32_MAX);
}

// ---------------------------------------------------------------------------
// Weight (Priority) tests
// ---------------------------------------------------------------------------

using coverwise::model::WeightConfig;

TEST(IntegrationTest, WeightDoesNotBreakCoverage) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari"}, {}},
      {"arch", {"x86", "arm"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;
  opts.weights.entries["os"]["win"] = 10.0;
  opts.weights.entries["os"]["mac"] = 5.0;
  opts.weights.entries["browser"]["chrome"] = 20.0;

  auto result = Generate(opts);

  // Coverage must still be 100%.
  auto report = ValidateCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_EQ(report.uncovered.size(), 0u);
}

TEST(IntegrationTest, WeightAffectsOutput) {
  // Generate twice: once with heavy weight on "win", once without.
  // The weighted run should have "win" appearing more frequently in early tests.
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari"}, {}},
      {"mode", {"dark", "light"}, {}},
  };
  opts.strength = 2;
  opts.seed = 100;

  auto result_no_weight = Generate(opts);

  opts.weights.entries["os"]["win"] = 100.0;
  opts.weights.entries["os"]["mac"] = 1.0;
  opts.weights.entries["os"]["linux"] = 1.0;

  auto result_weighted = Generate(opts);

  // Both must achieve 100% coverage.
  auto report1 = ValidateCoverage(opts.parameters, result_no_weight.tests, opts.strength);
  auto report2 = ValidateCoverage(opts.parameters, result_weighted.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report1.coverage_ratio, 1.0);
  EXPECT_DOUBLE_EQ(report2.coverage_ratio, 1.0);

  // Count how many times "win" (index 0) appears in os parameter (index 0)
  // in the first half of tests.
  auto count_win = [](const std::vector<TestCase>& tests, size_t limit) -> uint32_t {
    uint32_t count = 0;
    for (size_t i = 0; i < std::min(limit, tests.size()); ++i) {
      if (tests[i].values[0] == 0) ++count;
    }
    return count;
  };

  size_t half = std::min(result_weighted.tests.size(), result_no_weight.tests.size()) / 2;
  if (half > 0) {
    uint32_t weighted_wins = count_win(result_weighted.tests, half);
    uint32_t unweighted_wins = count_win(result_no_weight.tests, half);
    // Weighted should have at least as many "win" appearances.
    // This is a soft check; the weight is a hint, not a guarantee for every seed.
    EXPECT_GE(weighted_wins, unweighted_wins)
        << "Expected weighted run to prefer 'win' in early tests";
  }
}

TEST(IntegrationTest, WeightWithConstraints) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari", "ie"}, {}},
  };
  opts.constraint_expressions = {"IF os = mac THEN browser != ie"};
  opts.strength = 2;
  opts.seed = 777;
  opts.weights.entries["os"]["mac"] = 3.0;
  opts.weights.entries["browser"]["chrome"] = 3.0;

  auto result = Generate(opts);

  // Generator coverage accounts for constraint exclusions.
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());

  // Verify constraint is respected.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);
    bool is_ie = (tc.values[1] == 3);
    if (is_mac) {
      EXPECT_FALSE(is_ie) << "Constraint violation: os=mac, browser=ie";
    }
  }
}

// ---------------------------------------------------------------------------
// Preview Stats (EstimateModel) tests
// ---------------------------------------------------------------------------

using coverwise::core::EstimateModel;
using coverwise::model::ModelStats;

TEST(IntegrationTest, PreviewStatsBasic) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  opts.strength = 2;

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.parameter_count, 3u);
  EXPECT_EQ(stats.total_values, 6u);
  EXPECT_EQ(stats.strength, 2u);
  // C(3,2) * 2*2 = 3 * 4 = 12 tuples
  EXPECT_EQ(stats.total_tuples, 12u);
  EXPECT_GT(stats.estimated_tests, 0u);
  EXPECT_EQ(stats.sub_model_count, 0u);
  EXPECT_EQ(stats.constraint_count, 0u);

  ASSERT_EQ(stats.parameters.size(), 3u);
  EXPECT_EQ(stats.parameters[0].name, "A");
  EXPECT_EQ(stats.parameters[0].value_count, 2u);
  EXPECT_EQ(stats.parameters[0].invalid_count, 0u);
}

TEST(IntegrationTest, PreviewStatsMixedParams) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1", "b2"}, {}},
      {"C", {"c0", "c1", "c2", "c3"}, {}},
      {"D", {"d0", "d1"}, {}},
  };
  opts.strength = 2;

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.parameter_count, 4u);
  EXPECT_EQ(stats.total_values, 11u);
  EXPECT_EQ(stats.strength, 2u);
  // C(4,2) pairs: 6 combinations
  // (A,B): 6, (A,C): 8, (A,D): 4, (B,C): 12, (B,D): 6, (C,D): 8 = 44
  EXPECT_EQ(stats.total_tuples, 44u);
  EXPECT_GT(stats.estimated_tests, 0u);

  ASSERT_EQ(stats.parameters.size(), 4u);
  EXPECT_EQ(stats.parameters[2].name, "C");
  EXPECT_EQ(stats.parameters[2].value_count, 4u);
}

TEST(IntegrationTest, PreviewStatsWithInvalid) {
  GenerateOptions opts;
  opts.parameters = {
      Parameter("os", {"win", "mac", "linux"}, {false, false, false}),
      Parameter("browser", {"chrome", "firefox", "ie"}, {false, false, true}),
  };
  opts.strength = 2;

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.parameter_count, 2u);
  EXPECT_EQ(stats.total_values, 6u);
  ASSERT_EQ(stats.parameters.size(), 2u);
  EXPECT_EQ(stats.parameters[0].invalid_count, 0u);
  EXPECT_EQ(stats.parameters[1].invalid_count, 1u);
}

TEST(IntegrationTest, PreviewStatsWithConstraints) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac"}, {}},
      {"browser", {"chrome", "safari"}, {}},
  };
  opts.constraint_expressions = {"IF os = mac THEN browser != chrome"};
  opts.strength = 2;

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.constraint_count, 1u);
  EXPECT_EQ(stats.total_tuples, 4u);
}

TEST(IntegrationTest, PreviewStatsEstimateReasonable) {
  // Verify that estimated_tests is in a reasonable range compared to actual generation.
  GenerateOptions opts;
  opts.parameters = {
      {"P1", {"a", "b", "c"}, {}},
      {"P2", {"x", "y", "z"}, {}},
      {"P3", {"1", "2", "3"}, {}},
      {"P4", {"p", "q", "r"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto stats = EstimateModel(opts);
  auto result = Generate(opts);

  // Estimated should be at least as many as actual (it is an upper bound).
  // Allow some margin: estimated should be within a factor of 5 of actual.
  uint32_t actual = static_cast<uint32_t>(result.tests.size());
  EXPECT_GT(stats.estimated_tests, 0u);
  EXPECT_LE(actual, stats.estimated_tests * 2)
      << "Actual tests (" << actual << ") much larger than estimate (" << stats.estimated_tests
      << ")";
}

TEST(IntegrationTest, EquivalenceClassGeneratorIntegration) {
  // Verify that Generate() + ComputeClassCoverage works when classes are defined.
  GenerateOptions opts;
  opts.parameters = {
      MakeClassParam("age", {"5", "15", "25", "35", "65"},
                     {"child", "teen", "adult", "adult", "senior"}),
      MakeClassParam("income", {"20k", "50k", "100k"}, {"low", "mid", "high"}),
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Class coverage is no longer computed inside Generate() (to avoid core/ -> validator/
  // dependency). Compute it explicitly here.
  auto class_report = ComputeClassCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_EQ(class_report.total_class_tuples, 12u);
  // Generation should cover all class combos since it generates all value pairs.
  EXPECT_EQ(class_report.covered_class_tuples, 12u);
  EXPECT_DOUBLE_EQ(class_report.coverage_ratio, 1.0);
}

TEST(IntegrationTest, EquivalenceClassGeneratorNoClasses) {
  // Without classes, ComputeClassCoverage should return zero tuples.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Class coverage is no longer computed inside Generate(). Verify that ComputeClassCoverage
  // returns zero tuples when no equivalence classes are defined.
  auto class_report = ComputeClassCoverage(opts.parameters, result.tests, opts.strength);
  EXPECT_EQ(class_report.total_class_tuples, 0u);
}
