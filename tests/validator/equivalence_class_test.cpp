#include <gtest/gtest.h>

#include "core/generator.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/coverage_validator.h"

using coverwise::core::Generate;
using coverwise::core::GenerateOptions;
using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::validator::ComputeClassCoverage;

namespace {

/// @brief Helper to create a parameter with equivalence classes.
Parameter MakeClassParam(const std::string& name, std::vector<std::string> values,
                         std::vector<std::string> classes) {
  Parameter p(name, std::move(values));
  p.set_equivalence_classes(std::move(classes));
  return p;
}

}  // namespace

TEST(EquivalenceClassTest, Basic) {
  // age: 5=child, 15=teen, 25=adult, 35=adult, 65=senior
  // browser: chrome, firefox (no classes)
  std::vector<Parameter> params = {
      MakeClassParam("age", {"5", "15", "25", "35", "65"},
                     {"child", "teen", "adult", "adult", "senior"}),
      {"browser", {"chrome", "firefox"}, {}},
  };

  // Create test cases that cover some class combinations.
  std::vector<TestCase> tests = {
      {{0, 0}},  // age=5(child), browser=chrome
      {{1, 1}},  // age=15(teen), browser=firefox
      {{2, 0}},  // age=25(adult), browser=chrome
      {{4, 1}},  // age=65(senior), browser=firefox
  };

  // Only "age" has classes. With strength 2, we need class combos of
  // (age-class, browser-value) but browser has no classes.
  // Since only parameters WITH classes are considered, and there is only 1
  // such parameter, effective_strength = min(2, 1) = 1.
  // So we enumerate single-parameter class tuples for age: child, teen, adult, senior = 4.
  auto report = ComputeClassCoverage(params, tests, 2);

  EXPECT_EQ(report.total_class_tuples, 4u);
  EXPECT_EQ(report.covered_class_tuples, 4u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
}

TEST(EquivalenceClassTest, FullCoverageTwoClassParams) {
  // Two parameters with classes: pairwise class coverage.
  std::vector<Parameter> params = {
      MakeClassParam("age", {"5", "15", "25", "35", "65"},
                     {"child", "teen", "adult", "adult", "senior"}),
      MakeClassParam("income", {"20k", "50k", "100k"}, {"low", "mid", "high"}),
  };

  // Classes: age has 4 unique classes, income has 3.
  // Pairwise class tuples: 4 * 3 = 12.
  // Create tests covering all 12 combinations.
  std::vector<TestCase> tests = {
      {{0, 0}},  // child, low
      {{0, 1}},  // child, mid
      {{0, 2}},  // child, high
      {{1, 0}},  // teen, low
      {{1, 1}},  // teen, mid
      {{1, 2}},  // teen, high
      {{2, 0}},  // adult, low
      {{2, 1}},  // adult, mid
      {{2, 2}},  // adult, high
      {{4, 0}},  // senior, low
      {{4, 1}},  // senior, mid
      {{4, 2}},  // senior, high
  };

  auto report = ComputeClassCoverage(params, tests, 2);

  EXPECT_EQ(report.total_class_tuples, 12u);
  EXPECT_EQ(report.covered_class_tuples, 12u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
}

TEST(EquivalenceClassTest, PartialCoverage) {
  std::vector<Parameter> params = {
      MakeClassParam("age", {"5", "15", "25", "35", "65"},
                     {"child", "teen", "adult", "adult", "senior"}),
      MakeClassParam("income", {"20k", "50k", "100k"}, {"low", "mid", "high"}),
  };

  // Only cover some class combinations.
  std::vector<TestCase> tests = {
      {{0, 0}},  // child, low
      {{1, 1}},  // teen, mid
      {{2, 2}},  // adult, high
  };

  auto report = ComputeClassCoverage(params, tests, 2);

  EXPECT_EQ(report.total_class_tuples, 12u);
  EXPECT_EQ(report.covered_class_tuples, 3u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 3.0 / 12.0);
}

TEST(EquivalenceClassTest, NoClasses) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  std::vector<TestCase> tests = {{{0, 0}}, {{1, 1}}};

  auto report = ComputeClassCoverage(params, tests, 2);

  // No classes defined, everything should be zero.
  EXPECT_EQ(report.total_class_tuples, 0u);
  EXPECT_EQ(report.covered_class_tuples, 0u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 0.0);
}

TEST(EquivalenceClassTest, GeneratorIntegration) {
  // Verify that Generate() populates class_coverage when classes are defined.
  GenerateOptions opts;
  opts.parameters = {
      MakeClassParam("age", {"5", "15", "25", "35", "65"},
                     {"child", "teen", "adult", "adult", "senior"}),
      MakeClassParam("income", {"20k", "50k", "100k"}, {"low", "mid", "high"}),
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_TRUE(result.has_class_coverage);
  EXPECT_EQ(result.class_coverage.total_class_tuples, 12u);
  // Generation should cover all class combos since it generates all value pairs.
  EXPECT_EQ(result.class_coverage.covered_class_tuples, 12u);
  EXPECT_DOUBLE_EQ(result.class_coverage.class_coverage_ratio, 1.0);
}

TEST(EquivalenceClassTest, GeneratorNoClasses) {
  // Without classes, has_class_coverage should be false.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  EXPECT_FALSE(result.has_class_coverage);
  EXPECT_EQ(result.class_coverage.total_class_tuples, 0u);
}

TEST(EquivalenceClassTest, MultipleValuesInSameClass) {
  // Both "25" and "35" are "adult" — a test with either should cover the adult class.
  std::vector<Parameter> params = {
      MakeClassParam("age", {"25", "35"}, {"adult", "adult"}),
      MakeClassParam("size", {"S", "L"}, {"small", "large"}),
  };

  // Test with age=25 covers "adult" class, test with age=35 also covers "adult".
  // Only 1 unique class for age, 2 for size: 1*2 = 2 class tuples.
  std::vector<TestCase> tests = {
      {{0, 0}},  // adult, small
  };

  auto report = ComputeClassCoverage(params, tests, 2);

  EXPECT_EQ(report.total_class_tuples, 2u);
  EXPECT_EQ(report.covered_class_tuples, 1u);

  // Add second test to cover remaining.
  tests.push_back({{1, 1}});  // adult, large
  report = ComputeClassCoverage(params, tests, 2);

  EXPECT_EQ(report.covered_class_tuples, 2u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
}
