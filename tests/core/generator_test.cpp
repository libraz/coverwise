#include "core/generator.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "model/parameter.h"
#include "model/test_case.h"

using coverwise::core::EstimateModel;
using coverwise::core::Extend;
using coverwise::core::Generate;
using coverwise::model::ExtendMode;
using coverwise::model::GenerateOptions;
using coverwise::model::ModelStats;
using coverwise::model::Parameter;
using coverwise::model::SubModel;
using coverwise::model::TestCase;
using coverwise::model::WeightConfig;

// ---------------------------------------------------------------------------
// Generate tests
// ---------------------------------------------------------------------------

TEST(GeneratorTest, BasicPairwiseTwoByTwo) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_EQ(result.stats.total_tuples, 4u);  // 2 * 2
  EXPECT_EQ(result.stats.covered_tuples, 4u);
  EXPECT_GE(result.tests.size(), 2u);  // Need at least 2 tests for 2x2
}

TEST(GeneratorTest, ThreeParamsVaryingValues) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox"}, {}},
      {"arch", {"x64", "arm"}, {}},
  };
  opts.strength = 2;
  opts.seed = 7;

  auto result = Generate(opts);

  // C(3,2)=3 pairs: (os,browser)=6, (os,arch)=6, (browser,arch)=4 => 16 tuples
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_EQ(result.stats.total_tuples, 16u);
  EXPECT_EQ(result.stats.covered_tuples, 16u);
}

TEST(GeneratorTest, DeterminismSameSeed) {
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

TEST(GeneratorTest, DifferentSeedsMayDiffer) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a", "b", "c"}, {}},
      {"B", {"x", "y", "z"}, {}},
      {"C", {"1", "2", "3"}, {}},
  };
  opts.strength = 2;

  opts.seed = 1;
  auto result1 = Generate(opts);

  opts.seed = 999;
  auto result2 = Generate(opts);

  // Both must achieve full coverage.
  EXPECT_DOUBLE_EQ(result1.coverage, 1.0);
  EXPECT_DOUBLE_EQ(result2.coverage, 1.0);

  // With different seeds, test ordering may differ. Check at least one difference
  // exists somewhere (test values or order). This is probabilistic but very likely.
  bool any_diff = (result1.tests.size() != result2.tests.size());
  if (!any_diff) {
    for (size_t i = 0; i < result1.tests.size(); ++i) {
      if (result1.tests[i].values != result2.tests[i].values) {
        any_diff = true;
        break;
      }
    }
  }
  // Not a hard failure since identical output from different seeds is theoretically
  // possible, but extremely unlikely for 3x3x3 pairwise.
  EXPECT_TRUE(any_diff) << "Different seeds produced identical output (unlikely but possible)";
}

TEST(GeneratorTest, WithConstraints) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari", "ie"}, {}},
  };
  opts.constraint_expressions = {"IF os=mac THEN browser!=ie"};
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.warnings.empty());

  // Verify no test case violates the constraint: os=mac AND browser=ie.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);
    bool is_ie = (tc.values[1] == 3);
    EXPECT_FALSE(is_mac && is_ie) << "Constraint violation: os=mac, browser=ie";
  }
}

TEST(GeneratorTest, MaxTestsLimit) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "a2", "a3"}, {}},
      {"B", {"b0", "b1", "b2", "b3"}, {}},
      {"C", {"c0", "c1", "c2", "c3"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;
  opts.max_tests = 3;  // Too few to cover all 48 tuples.

  auto result = Generate(opts);

  EXPECT_LE(result.tests.size(), 3u);
  EXPECT_LT(result.coverage, 1.0);
  EXPECT_FALSE(result.uncovered.empty());
  EXPECT_GT(result.stats.total_tuples, result.stats.covered_tuples);
}

TEST(GeneratorTest, SeedTestsIncluded) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  TestCase seed1;
  seed1.values = {0, 0, 0};
  TestCase seed2;
  seed2.values = {1, 1, 1};
  opts.seeds = {seed1, seed2};

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

  // Seed tests should appear in the result.
  EXPECT_GE(result.tests.size(), 2u);

  // Verify the seed test values are present in the output.
  bool found_seed1 = false;
  bool found_seed2 = false;
  for (const auto& tc : result.tests) {
    if (tc.values == std::vector<uint32_t>{0, 0, 0}) found_seed1 = true;
    if (tc.values == std::vector<uint32_t>{1, 1, 1}) found_seed2 = true;
  }
  EXPECT_TRUE(found_seed1) << "Seed test {0,0,0} not found in output";
  EXPECT_TRUE(found_seed2) << "Seed test {1,1,1} not found in output";
}

TEST(GeneratorTest, SubModelHigherStrength) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari"}, {}},
      {"arch", {"x64", "arm"}, {}},
      {"lang", {"en", "ja"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  SubModel sm;
  sm.parameter_names = {"os", "browser", "arch"};
  sm.strength = 3;
  opts.sub_models = {sm};

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.warnings.empty());

  // More tests are needed for 3-wise sub-model coverage compared to pure pairwise.
  GenerateOptions opts_no_sub = opts;
  opts_no_sub.sub_models.clear();
  auto result_no_sub = Generate(opts_no_sub);
  EXPECT_GE(result.tests.size(), result_no_sub.tests.size());
}

TEST(GeneratorTest, NegativeTesting) {
  GenerateOptions opts;
  opts.parameters = {
      {"browser", {"chrome", "safari", "ie6"}, {false, false, true}},
      {"os", {"win", "mac"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  // Positive tests should achieve full coverage of valid tuples.
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);

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

TEST(GeneratorTest, LargeModelFullCoverage) {
  GenerateOptions opts;
  opts.parameters = {
      {"P1", {"a", "b", "c"}, {}}, {"P2", {"x", "y", "z"}, {}}, {"P3", {"1", "2", "3"}, {}},
      {"P4", {"p", "q"}, {}},      {"P5", {"m", "n"}, {}},      {"P6", {"u", "v"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_EQ(result.stats.total_tuples, result.stats.covered_tuples);
  EXPECT_GT(result.tests.size(), 0u);
}

// ---------------------------------------------------------------------------
// Extend tests
// ---------------------------------------------------------------------------

TEST(GeneratorExtendTest, ExtendEmptyIsLikeGenerate) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  std::vector<TestCase> empty_existing;
  auto result = Extend(empty_existing, opts, ExtendMode::kStrict);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_EQ(result.stats.total_tuples, 4u);
}

TEST(GeneratorExtendTest, ExtendPartialImprovesCoverage) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  // Start with a single test that covers a few tuples but not all.
  std::vector<TestCase> existing = {TestCase{{0, 0, 0}}};
  auto result = Extend(existing, opts, ExtendMode::kStrict);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());

  // The existing test should be included.
  bool found = false;
  for (const auto& tc : result.tests) {
    if (tc.values == std::vector<uint32_t>{0, 0, 0}) {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Existing test {0,0,0} should be preserved";
}

// ---------------------------------------------------------------------------
// EstimateModel tests
// ---------------------------------------------------------------------------

TEST(EstimateModelTest, BasicStats) {
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

TEST(EstimateModelTest, WithSubModels) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  opts.strength = 2;

  SubModel sm;
  sm.parameter_names = {"A", "B"};
  sm.strength = 3;
  opts.sub_models = {sm};

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.parameter_count, 3u);
  EXPECT_EQ(stats.sub_model_count, 1u);
}

TEST(EstimateModelTest, TotalTuplesMatchesFormula) {
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
  // C(4,2)=6 pairs: (A,B)=6, (A,C)=8, (A,D)=4, (B,C)=12, (B,D)=6, (C,D)=8 => 44
  EXPECT_EQ(stats.total_tuples, 44u);
}

// ---------------------------------------------------------------------------
// WeightConfig tests
// ---------------------------------------------------------------------------

TEST(WeightConfigTest, GetWeightReturnsConfiguredValue) {
  WeightConfig wc;
  wc.entries["os"]["win"] = 10.0;
  wc.entries["os"]["mac"] = 5.0;
  wc.entries["browser"]["chrome"] = 20.0;

  EXPECT_DOUBLE_EQ(wc.GetWeight("os", "win"), 10.0);
  EXPECT_DOUBLE_EQ(wc.GetWeight("os", "mac"), 5.0);
  EXPECT_DOUBLE_EQ(wc.GetWeight("browser", "chrome"), 20.0);
}

TEST(WeightConfigTest, GetWeightReturnsDefaultForUnconfigured) {
  WeightConfig wc;
  wc.entries["os"]["win"] = 10.0;

  // Unconfigured value in configured param.
  EXPECT_DOUBLE_EQ(wc.GetWeight("os", "linux"), 1.0);

  // Completely unconfigured param.
  EXPECT_DOUBLE_EQ(wc.GetWeight("browser", "chrome"), 1.0);
}

TEST(WeightConfigTest, EmptyReturnsTrueWhenNoEntries) {
  WeightConfig wc;
  EXPECT_TRUE(wc.empty());

  wc.entries["os"]["win"] = 1.0;
  EXPECT_FALSE(wc.empty());
}

// ---------------------------------------------------------------------------
// Generator edge cases
// ---------------------------------------------------------------------------

// Edge: empty parameters → coverage 1.0, 0 tests
TEST(GeneratorEdgeCaseTest, EmptyParameters) {
  GenerateOptions opts;
  opts.strength = 2;
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.tests.empty());
  EXPECT_EQ(result.stats.total_tuples, 0u);
}

// Edge: single parameter → no pairs, coverage 1.0
TEST(GeneratorEdgeCaseTest, SingleParameter) {
  GenerateOptions opts;
  opts.parameters = {{"os", {"win", "mac", "linux"}, {}}};
  opts.strength = 2;
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_EQ(result.stats.total_tuples, 0u);
}

// Edge: strength > param count → coverage 1.0
TEST(GeneratorEdgeCaseTest, StrengthExceedsParamCount) {
  GenerateOptions opts;
  opts.parameters = {{"a", {"1", "2"}, {}}, {"b", {"1", "2"}, {}}};
  opts.strength = 5;
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.tests.empty());
}

// Edge: strength = 1 → each value appears at least once
TEST(GeneratorEdgeCaseTest, StrengthOne) {
  GenerateOptions opts;
  opts.parameters = {
      {"a", {"1", "2", "3"}, {}},
      {"b", {"x", "y"}, {}},
  };
  opts.strength = 1;
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  // All 5 values should be covered
  EXPECT_EQ(result.stats.total_tuples, 5u);
}

// Edge: maxTests = 1
TEST(GeneratorEdgeCaseTest, MaxTestsOne) {
  GenerateOptions opts;
  opts.parameters = {
      {"a", {"1", "2", "3"}, {}},
      {"b", {"1", "2", "3"}, {}},
  };
  opts.max_tests = 1;
  auto result = Generate(opts);
  EXPECT_EQ(result.tests.size(), 1u);
  EXPECT_LT(result.coverage, 1.0);
  EXPECT_FALSE(result.uncovered.empty());
}

// Edge: seeds fill max_tests → no additional tests generated
TEST(GeneratorEdgeCaseTest, SeedsFillMaxTests) {
  GenerateOptions opts;
  opts.parameters = {{"a", {"1", "2"}, {}}, {"b", {"1", "2"}, {}}};
  opts.seeds = {TestCase{{0, 0}}, TestCase{{1, 1}}};
  opts.max_tests = 2;
  auto result = Generate(opts);
  EXPECT_EQ(result.tests.size(), 2u);
}

// Edge: parameter with single value
TEST(GeneratorEdgeCaseTest, ParameterWithSingleValue) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win"}, {}},
      {"browser", {"chrome", "firefox"}, {}},
  };
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  // All tests must have os=win (index 0)
  for (const auto& tc : result.tests) {
    EXPECT_EQ(tc.values[0], 0u);
  }
}

// ---------------------------------------------------------------------------
// EstimateModel edge cases
// ---------------------------------------------------------------------------

// Edge: EstimateModel with 0 parameters
TEST(EstimateModelEdgeTest, ZeroParameters) {
  GenerateOptions opts;
  opts.strength = 2;
  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.parameter_count, 0u);
  EXPECT_EQ(stats.total_values, 0u);
  EXPECT_EQ(stats.total_tuples, 0u);
}

// Edge: EstimateModel with 1 parameter
TEST(EstimateModelEdgeTest, SingleParameter) {
  GenerateOptions opts;
  opts.parameters = {{"os", {"win", "mac", "linux"}, {}}};
  opts.strength = 2;
  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.parameter_count, 1u);
  EXPECT_EQ(stats.total_tuples, 0u);  // C(1,2) = 0
}

// Edge: EstimateModel large values don't overflow
TEST(EstimateModelEdgeTest, LargeValuesNoOverflow) {
  GenerateOptions opts;
  // 10 params × 100 values at strength 3 → 100^3 = 1M, should not overflow
  for (int i = 0; i < 10; i++) {
    std::vector<std::string> vals;
    for (int j = 0; j < 100; j++) vals.push_back(std::to_string(j));
    opts.parameters.push_back({"p" + std::to_string(i), std::move(vals), {}});
  }
  opts.strength = 3;
  auto stats = EstimateModel(opts);
  EXPECT_GT(stats.estimated_tests, 0u);
  // Should not have wrapped around to a small number
  EXPECT_GE(stats.estimated_tests, 1000u);
}
