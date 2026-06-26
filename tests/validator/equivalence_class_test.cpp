#include <gtest/gtest.h>

#include <utility>
#include <vector>

#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/coverage_validator.h"

using coverwise::model::Constraint;
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

TEST(EquivalenceClassTest, ConstraintExcludesUnsatisfiableClassTuple) {
  // os = {win(desktop), mac(apple)}, browser = {chrome(modern), ie(legacy)}.
  // Constraint: IF os=mac THEN browser!=ie. The class tuple (apple, legacy) has
  // only one representative (mac, ie), which the constraint forbids, so it must
  // be excluded from the class-coverage universe. A suite covering the three
  // remaining valid class tuples must then report classCoverageRatio == 1.0.
  Parameter os("os", {"win", "mac"});
  os.set_equivalence_classes({"desktop", "apple"});
  Parameter browser("browser", {"chrome", "ie"});
  browser.set_equivalence_classes({"modern", "legacy"});
  std::vector<Parameter> params = {os, browser};

  auto parse = coverwise::model::ParseConstraint("IF os=mac THEN browser!=ie", params);
  ASSERT_TRUE(parse.error.ok()) << parse.error.message << ": " << parse.error.detail;
  std::vector<Constraint> constraints;
  constraints.push_back(std::move(parse.constraint));

  // Cover the three valid class tuples:
  //   (desktop, modern) via (win, chrome)
  //   (desktop, legacy) via (win, ie)
  //   (apple, modern)   via (mac, chrome)
  std::vector<TestCase> tests = {
      TestCase{{0, 0}},  // win, chrome
      TestCase{{0, 1}},  // win, ie
      TestCase{{1, 0}},  // mac, chrome
  };

  // Without constraint exclusion the universe would be 4 class tuples and this
  // suite would report ratio 0.75. With exclusion it is 3 / 3 = 1.0.
  auto report = ComputeClassCoverage(params, tests, 2, constraints);

  EXPECT_EQ(report.total_class_tuples, 3u);
  EXPECT_EQ(report.covered_class_tuples, 3u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);

  // Sanity: without constraints the same suite is incomplete (4 tuples, 3 covered).
  auto unconstrained = ComputeClassCoverage(params, tests, 2);
  EXPECT_EQ(unconstrained.total_class_tuples, 4u);
  EXPECT_EQ(unconstrained.covered_class_tuples, 3u);
  EXPECT_LT(unconstrained.coverage_ratio, 1.0);
}

TEST(EquivalenceClassTest, InvalidValueExcludesUnsatisfiableClassTuple) {
  // os = {win(desktop), mac(apple), ie6(legacy, invalid)}.
  // browser = {chrome(modern), safari(webkit)}.
  // The only value in the "legacy" class is invalid, so every class tuple
  // requiring os=legacy has no valid representative and is excluded. The
  // remaining {desktop, apple} x {modern, webkit} = 4 class tuples form the
  // universe; a suite covering them must report ratio 1.0.
  Parameter os("os", {"win", "mac", "ie6"}, {false, false, true});
  os.set_equivalence_classes({"desktop", "apple", "legacy"});
  Parameter browser("browser", {"chrome", "safari"});
  browser.set_equivalence_classes({"modern", "webkit"});
  std::vector<Parameter> params = {os, browser};

  std::vector<TestCase> tests = {
      TestCase{{0, 0}},  // win, chrome   -> desktop, modern
      TestCase{{0, 1}},  // win, safari   -> desktop, webkit
      TestCase{{1, 0}},  // mac, chrome   -> apple, modern
      TestCase{{1, 1}},  // mac, safari   -> apple, webkit
  };

  auto report = ComputeClassCoverage(params, tests, 2);

  EXPECT_EQ(report.total_class_tuples, 4u);
  EXPECT_EQ(report.covered_class_tuples, 4u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
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
