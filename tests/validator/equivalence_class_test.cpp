#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/generator.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/generate_options.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/coverage_validator.h"

using coverwise::core::Generate;
using coverwise::model::Constraint;
using coverwise::model::ConstraintResult;
using coverwise::model::GenerateOptions;
using coverwise::model::GenerateResult;
using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::validator::AnnotateClassCoverage;
using coverwise::validator::ComputeClassCoverage;

namespace {

/// @brief Helper to create a parameter with equivalence classes.
Parameter MakeClassParam(const std::string& name, std::vector<std::string> values,
                         std::vector<std::string> classes) {
  Parameter p(name, std::move(values));
  p.set_equivalence_classes(std::move(classes));
  return p;
}

class LateClassWitnessConstraint final : public coverwise::model::ConstraintNode {
 public:
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override {
    for (uint32_t value : assignment) {
      if (value == coverwise::model::kUnassigned) return ConstraintResult::kUnknown;
    }
    if (assignment.back() == 0) return ConstraintResult::kTrue;
    for (size_t i = 0; i + 1 < assignment.size(); ++i) {
      if (assignment[i] != 1) return ConstraintResult::kFalse;
    }
    return ConstraintResult::kTrue;
  }
};

/// @brief Rejects any assignment pinning both parameters inside their large
///        class, and counts how often it is asked.
///
/// The escape value each parameter also carries keeps the model satisfiable, so
/// the class tuple over the two large classes is the only infeasible one — and
/// its representatives are the whole cross product of those classes.
class WideClassRejectConstraint final : public coverwise::model::ConstraintNode {
 public:
  explicit WideClassRejectConstraint(uint32_t escape_index) : escape_index_(escape_index) {}

  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override {
    ++evaluations;
    if (assignment[0] == coverwise::model::kUnassigned ||
        assignment[1] == coverwise::model::kUnassigned) {
      return ConstraintResult::kUnknown;
    }
    return (assignment[0] < escape_index_ && assignment[1] < escape_index_)
               ? ConstraintResult::kFalse
               : ConstraintResult::kTrue;
  }

  mutable uint64_t evaluations = 0;

 private:
  uint32_t escape_index_;
};

/// @brief Expression whose only cheap witnesses are gate="open" and pick="cheap".
///
/// With both fixed the other way the expression stays undecided until "relief"
/// is assigned, and "none" — its only satisfying value — is marked invalid, so
/// proving the branch unsatisfiable costs more than the search budget allows.
constexpr const char* kCostlyRepresentativeExpression =
    "gate=\"open\" OR pick=\"cheap\" OR relief=\"none\"";

/// @brief Model whose "same" class holds one cheap and one costly representative.
///
/// @param cheap_first Places the cheap representative at value index 0 when
///        true, and at value index 1 when false. The class tuple is feasible
///        either way, so both orders must produce the same verdict.
///
/// Every filler parameter has more valid values than "relief", so a search that
/// orders parameters by ascending domain size settles the branch immediately
/// while one walking parameters in declaration order does not.
std::vector<Parameter> MakeRepresentativeOrderModel(bool cheap_first) {
  Parameter gate("gate", {"open", "shut"});
  gate.set_equivalence_classes({"open_class", "shut_class"});
  Parameter pick("pick", cheap_first ? std::vector<std::string>{"cheap", "costly"}
                                     : std::vector<std::string>{"costly", "cheap"});
  pick.set_equivalence_classes({"same", "same"});
  std::vector<Parameter> params = {std::move(gate), std::move(pick)};
  for (uint32_t index = 0; index < 14; ++index) {
    params.push_back(Parameter{"f" + std::to_string(index), {"a", "b", "c"}});
  }
  params.push_back(Parameter{"relief", {"r0", "r1", "none"}, {false, false, true}});
  return params;
}

std::vector<Constraint> ParseSingleConstraint(const std::string& expression,
                                              const std::vector<Parameter>& params) {
  auto parse = coverwise::model::ParseConstraint(expression, params);
  EXPECT_TRUE(parse.error.ok()) << parse.error.message << ": " << parse.error.detail;
  std::vector<Constraint> constraints;
  constraints.push_back(std::move(parse.constraint));
  return constraints;
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

  // No classes defined: the class-tuple universe is empty, so class coverage is
  // vacuously complete (1.0), matching the empty-universe handling elsewhere.
  // A ratio of 0.0 is reserved for the error exits, so an empty universe is
  // told apart from a failure by the error, never by the ratio.
  EXPECT_EQ(report.total_class_tuples, 0u);
  EXPECT_EQ(report.covered_class_tuples, 0u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
  EXPECT_TRUE(report.error.ok()) << report.error.message;
}

TEST(EquivalenceClassTest, StrengthOutsideParameterRangeIsAnEmptyUniverse) {
  // The remaining empty-universe exit: a strength that cannot form a tuple. It
  // reports the same {0, 0, 1.0, ok} shape as a model without classes.
  std::vector<Parameter> params = {
      MakeClassParam("age", {"5", "25"}, {"child", "adult"}),
  };
  std::vector<TestCase> tests = {{{0}}, {{1}}};

  auto zero = ComputeClassCoverage(params, tests, 0);
  EXPECT_EQ(zero.total_class_tuples, 0u);
  EXPECT_EQ(zero.covered_class_tuples, 0u);
  EXPECT_DOUBLE_EQ(zero.coverage_ratio, 1.0);
  EXPECT_TRUE(zero.error.ok()) << zero.error.message;

  auto too_high = ComputeClassCoverage(params, tests, 2);
  EXPECT_EQ(too_high.total_class_tuples, 0u);
  EXPECT_EQ(too_high.covered_class_tuples, 0u);
  EXPECT_DOUBLE_EQ(too_high.coverage_ratio, 1.0);
  EXPECT_TRUE(too_high.error.ok()) << too_high.error.message;
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

TEST(EquivalenceClassTest, SearchBudgetExhaustionPropagatesToGenerateResult) {
  std::vector<Parameter> params;
  params.reserve(22);
  for (uint32_t index = 0; index < 22; ++index) {
    Parameter parameter{"P" + std::to_string(index), {"0", "1"}};
    if (index == 21) parameter.set_equivalence_classes({"", "hard"});
    params.push_back(std::move(parameter));
  }
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<LateClassWitnessConstraint>());
  GenerateResult result;

  AnnotateClassCoverage(result, params, 1, constraints);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(result.error.message, "Constraint search budget exceeded");
  EXPECT_FALSE(result.class_coverage.has_value());
}

TEST(EquivalenceClassTest, ClassTupleWitnessedByAValidTestIsNotSearched) {
  // The model of the previous test, plus a valid row covering the "hard" class.
  // That row is a complete assignment of valid values satisfying every
  // constraint, and its value in this parameter is a representative of the
  // class tuple, so the feasibility search has nothing left to establish.
  // Running it anyway spends the whole node budget and reports a covered tuple
  // as undecidable.
  std::vector<Parameter> params;
  params.reserve(22);
  for (uint32_t index = 0; index < 22; ++index) {
    Parameter parameter{"P" + std::to_string(index), {"0", "1"}};
    if (index == 21) parameter.set_equivalence_classes({"", "hard"});
    params.push_back(std::move(parameter));
  }
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<LateClassWitnessConstraint>());
  GenerateResult result;
  // Every parameter at its second value satisfies the constraint, and it is the
  // "hard" class that the last parameter then takes.
  result.tests.push_back(TestCase{std::vector<uint32_t>(22, 1)});

  AnnotateClassCoverage(result, params, 1, constraints);

  ASSERT_TRUE(result.error.ok()) << result.error.message << ": " << result.error.detail;
  ASSERT_TRUE(result.class_coverage.has_value());
  EXPECT_EQ(result.class_coverage->total_class_tuples, 1u);
  EXPECT_EQ(result.class_coverage->covered_class_tuples, 1u);
  EXPECT_DOUBLE_EQ(result.class_coverage->class_coverage_ratio, 1.0);
}

TEST(EquivalenceClassTest, ClassCoverageWarningComesFromTheSingleErrorMapping) {
  // ValidateParameters names the offending parameter and carries no detail, so
  // a warning assembled as "message: detail" would end in a dangling separator.
  Parameter os("os", {"win", "mac"}, {true, true});
  os.set_equivalence_classes({"desktop", "apple"});
  std::vector<Parameter> params = {std::move(os), Parameter{"browser", {"chrome"}}};
  GenerateResult result;

  AnnotateClassCoverage(result, params, 2);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  ASSERT_TRUE(result.error.detail.empty());
  ASSERT_EQ(result.warnings.size(), 1u);
  EXPECT_EQ(result.warnings[0], result.error.message);
}

TEST(EquivalenceClassTest, RepresentativeEnumerationStopsOnASharedNodeBudget) {
  // Both parameters carry one large class and one escape value. The model is
  // satisfiable through the escape values, so validation reaches the class
  // tuple over the two large classes — and that tuple's representatives are the
  // whole cross product of the two classes, every one of them rejected. Without
  // one budget shared across them, each cheap rejection starts a fresh budget
  // that never runs out and the enumeration walks all of them.
  constexpr uint32_t kClassValues = 4000;
  constexpr uint64_t kRepresentatives =
      static_cast<uint64_t>(kClassValues) * static_cast<uint64_t>(kClassValues);

  std::vector<Parameter> params;
  for (const char* name : {"left", "right"}) {
    std::vector<std::string> values;
    std::vector<std::string> classes;
    values.reserve(kClassValues + 1);
    classes.reserve(kClassValues + 1);
    for (uint32_t index = 0; index < kClassValues; ++index) {
      values.push_back(std::string(name) + std::to_string(index));
      classes.emplace_back("many");
    }
    values.emplace_back(std::string(name) + "_escape");
    classes.emplace_back("escape_class");
    Parameter parameter{name, std::move(values)};
    parameter.set_equivalence_classes(std::move(classes));
    params.push_back(std::move(parameter));
  }

  auto counted = std::make_unique<WideClassRejectConstraint>(kClassValues);
  const WideClassRejectConstraint* constraint = counted.get();
  std::vector<Constraint> constraints;
  constraints.push_back(std::move(counted));
  GenerateResult result;

  AnnotateClassCoverage(result, params, 2, constraints);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(result.error.message, "Constraint search budget exceeded");
  EXPECT_FALSE(result.class_coverage.has_value());

  // Every representative costs at least one search node, so a bounded total of
  // nodes is a bounded number of representatives: the enumeration has to stop
  // short of the cross product rather than sweep it.
  EXPECT_LT(constraint->evaluations, kRepresentatives) << "cross product=" << kRepresentatives;
}

TEST(EquivalenceClassTest, ClassTupleVerdictIgnoresRepresentativeOrder) {
  uint64_t cheap_first_total = 0;
  uint64_t costly_first_total = 0;

  for (bool cheap_first : {true, false}) {
    auto params = MakeRepresentativeOrderModel(cheap_first);
    auto constraints = ParseSingleConstraint(kCostlyRepresentativeExpression, params);
    GenerateResult result;

    AnnotateClassCoverage(result, params, 2, constraints);

    // A representative whose search runs out of budget must not decide the
    // tuple: the "same" class also holds a representative that is trivially
    // satisfiable, which makes the tuple feasible from either value order.
    ASSERT_TRUE(result.error.ok())
        << result.error.message << ": " << result.error.detail
        << " -- a budget-exceeded verdict on this model means one class tuple no "
           "longer affords more than a single full search, so the costly "
           "representative consumed the whole of kMaxClassTupleSearchNodes before "
           "the feasible one was reached";
    ASSERT_TRUE(result.class_coverage.has_value());
    (cheap_first ? cheap_first_total : costly_first_total) =
        result.class_coverage->total_class_tuples;
  }

  EXPECT_EQ(cheap_first_total, costly_first_total);
  EXPECT_EQ(cheap_first_total, 2u);
}

TEST(EquivalenceClassTest, GenerateCompletesWhenACostlyRepresentativeComesFirst) {
  uint64_t cheap_first_total = 0;
  uint64_t costly_first_total = 0;

  for (bool cheap_first : {true, false}) {
    GenerateOptions options;
    options.parameters = MakeRepresentativeOrderModel(cheap_first);
    options.strength = 2;
    options.constraint_expressions.emplace_back(kCostlyRepresentativeExpression);

    auto result = Generate(options);

    ASSERT_TRUE(result.error.ok()) << result.error.message << ": " << result.error.detail;
    EXPECT_FALSE(result.tests.empty());
    ASSERT_TRUE(result.class_coverage.has_value());
    (cheap_first ? cheap_first_total : costly_first_total) =
        result.class_coverage->total_class_tuples;
  }

  EXPECT_EQ(cheap_first_total, costly_first_total);
  EXPECT_EQ(cheap_first_total, 2u);
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

TEST(EquivalenceClassTest, LargeDistinctClassUniverseUsesIndexedProjection) {
  constexpr uint32_t kClasses = 1000;
  Parameter left{"left", {}};
  Parameter right{"right", {}};
  std::vector<std::string> left_classes;
  std::vector<std::string> right_classes;
  left.values.reserve(kClasses);
  right.values.reserve(kClasses);
  left_classes.reserve(kClasses);
  right_classes.reserve(kClasses);
  for (uint32_t index = 0; index < kClasses; ++index) {
    left.values.push_back("l" + std::to_string(index));
    right.values.push_back("r" + std::to_string(index));
    left_classes.push_back("lc" + std::to_string(index));
    right_classes.push_back("rc" + std::to_string(index));
  }
  left.set_equivalence_classes(std::move(left_classes));
  right.set_equivalence_classes(std::move(right_classes));

  auto report = ComputeClassCoverage({left, right}, {{{0, 0}}}, 2);

  EXPECT_TRUE(report.error.ok()) << report.error.message;
  EXPECT_EQ(report.total_class_tuples, 1'000'000u);
  EXPECT_EQ(report.covered_class_tuples, 1u);
}

// ---------------------------------------------------------------------------
// Cost of declaring equivalence classes
//
// Class coverage is annotated onto every generate result, so resolving a value
// to its class runs once per (combination, test, position). That resolution has
// to be a flat array read into the parameter's class domain.
// ---------------------------------------------------------------------------
namespace {

GenerateOptions ClassCostModel(uint32_t parameters, uint32_t values, uint32_t classes,
                               bool with_classes) {
  GenerateOptions options;
  for (uint32_t index = 0; index < parameters; ++index) {
    Parameter parameter;
    parameter.name = "p" + std::to_string(index);
    std::vector<std::string> class_names;
    for (uint32_t value = 0; value < values; ++value) {
      parameter.values.push_back("v" + std::to_string(value));
      class_names.push_back("c" + std::to_string(value % classes));
    }
    if (with_classes) parameter.set_equivalence_classes(std::move(class_names));
    options.parameters.push_back(std::move(parameter));
  }
  options.strength = 2;
  options.seed = 42;
  return options;
}

/// @brief Class-label lengths compared by the projection-cost test. The two are
///        128x apart, so a projection that touches the label bytes cannot come
///        out of the comparison looking flat.
constexpr size_t kShortClassNameLength = 2;
constexpr size_t kLongClassNameLength = 256;

/// @brief Parameters whose class names are padded to @p name_length characters.
std::vector<Parameter> PaddedClassParams(uint32_t parameters, uint32_t values, uint32_t classes,
                                         size_t name_length) {
  std::vector<Parameter> params;
  for (uint32_t index = 0; index < parameters; ++index) {
    Parameter parameter;
    parameter.name = "p" + std::to_string(index);
    std::vector<std::string> class_names;
    for (uint32_t value = 0; value < values; ++value) {
      parameter.values.push_back("v" + std::to_string(value));
      std::string class_name = "c" + std::to_string(value % classes);
      class_name.append(name_length > class_name.size() ? name_length - class_name.size() : 0, 'x');
      class_names.push_back(std::move(class_name));
    }
    parameter.set_equivalence_classes(std::move(class_names));
    params.push_back(std::move(parameter));
  }
  return params;
}

std::vector<TestCase> SpreadSuite(uint32_t parameters, uint32_t values, uint32_t count) {
  std::vector<TestCase> tests(count);
  for (uint32_t t = 0; t < count; ++t) {
    tests[t].values.resize(parameters);
    for (uint32_t index = 0; index < parameters; ++index) {
      tests[t].values[index] = (t * 7 + index * 5) % values;
    }
  }
  return tests;
}

/// @brief One timed ComputeClassCoverage run over @p params, in milliseconds.
double ClassCoverageMs(const std::vector<Parameter>& params, const std::vector<TestCase>& tests) {
  auto start = std::chrono::steady_clock::now();
  auto report = ComputeClassCoverage(params, tests, 2);
  auto elapsed =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  EXPECT_TRUE(report.error.ok()) << report.error.message;
  return elapsed;
}

/// @brief Fastest run of each model, sampling the two alternately.
///
/// Timing one model to completion and only then the other lets a shift in
/// machine load land wholly on whichever went second, which reads back as a
/// ratio neither model earned. Alternating puts both through the same load
/// window; taking each model's own minimum then keeps the quietest of them.
std::pair<double, double> FastestClassCoverageMsEach(const std::vector<Parameter>& first,
                                                     const std::vector<Parameter>& second,
                                                     const std::vector<TestCase>& tests,
                                                     int repetitions) {
  double first_best = 0.0;
  double second_best = 0.0;
  for (int i = 0; i < repetitions; ++i) {
    // Swap which model goes first on alternate rounds. Whichever runs first in a
    // round absorbs whatever the round itself costs — a scheduling slice ending,
    // a page fault on a grown buffer — and holding that position fixed would
    // charge it to the same model every time.
    const bool first_leads = (i % 2) == 0;
    double first_ms = first_leads ? ClassCoverageMs(first, tests) : 0.0;
    double second_ms = ClassCoverageMs(second, tests);
    if (!first_leads) first_ms = ClassCoverageMs(first, tests);
    if (i == 0 || first_ms < first_best) first_best = first_ms;
    if (i == 0 || second_ms < second_best) second_best = second_ms;
  }
  return {first_best, second_best};
}

}  // namespace

TEST(EquivalenceClassTest, DeclaringClassesCoversEveryClassTupleWithoutChangingTheSuite) {
  constexpr uint32_t kParameters = 24;
  constexpr uint32_t kValues = 12;
  constexpr uint32_t kClasses = 4;

  auto without_classes = ClassCostModel(kParameters, kValues, kClasses, false);
  auto with_classes = ClassCostModel(kParameters, kValues, kClasses, true);

  auto classified = Generate(with_classes);
  ASSERT_TRUE(classified.error.ok()) << classified.error.message;
  ASSERT_TRUE(classified.class_coverage.has_value());

  // C(24, 2) parameter pairs, each contributing kClasses^2 class tuples, and a
  // pairwise suite over 12 values per parameter covers every one of them.
  constexpr uint64_t kExpectedClassTuples =
      static_cast<uint64_t>(kParameters) * (kParameters - 1) / 2 * kClasses * kClasses;
  EXPECT_EQ(classified.class_coverage->total_class_tuples, kExpectedClassTuples);
  EXPECT_EQ(classified.class_coverage->covered_class_tuples, kExpectedClassTuples);
  EXPECT_DOUBLE_EQ(classified.class_coverage->class_coverage_ratio, 1.0);

  // Classes must not change the suite itself. Nothing on the generation or
  // scoring path reads them — they are supplied only to the after-the-fact
  // annotation — so the same parameters, strength and seed have to produce the
  // same rows in the same order, not merely as many of them. Comparing counts
  // alone would keep passing if generation started to branch on a declared
  // class and returned a different suite of the same size.
  auto plain = Generate(without_classes);
  ASSERT_TRUE(plain.error.ok()) << plain.error.message;
  EXPECT_FALSE(plain.class_coverage.has_value());
  ASSERT_EQ(classified.tests.size(), plain.tests.size());
  for (size_t row = 0; row < plain.tests.size(); ++row) {
    EXPECT_EQ(classified.tests[row].values, plain.tests[row].values) << "row " << row;
  }

  // Nothing here bounds classified generation time against plain: annotation is
  // a small enough share of generation that the ratio's noise on a loaded
  // parallel run is wider than the slowdown a projection regression adds, so it
  // cannot tell the two apart. ClassProjectionCostIsIndependentOfClassNameLength
  // holds the structural property instead — resolving a value to its class is a
  // flat array read — where the signal clears the noise.
}

TEST(EquivalenceClassTest, ClassProjectionCostIsIndependentOfClassNameLength) {
  // Resolving a value to its class runs once per (combination, test, position),
  // so it has to be a flat array read. A name-keyed hash lookup would instead
  // make the projection scale with how long the class labels happen to be —
  // a property of the model's vocabulary, not of its size.
  constexpr uint32_t kParameters = 24;
  constexpr uint32_t kValues = 12;
  constexpr uint32_t kClasses = 4;
  constexpr uint32_t kSuiteSize = 4000;
  // This gate separates a 3.4x regression, which is narrow next to what a
  // loaded parallel run does to a wall clock. It therefore has to buy its
  // separation with a precise measurement rather than a wide bound: the
  // fastest-of-N estimator converges as N grows, and at 15 the ratio holds
  // inside a few percent of 1.0 where at 5 it wandered by a third.
  constexpr int kRepetitions = 15;

  auto short_names = PaddedClassParams(kParameters, kValues, kClasses, kShortClassNameLength);
  auto long_names = PaddedClassParams(kParameters, kValues, kClasses, kLongClassNameLength);
  auto tests = SpreadSuite(kParameters, kValues, kSuiteSize);

  // Padding renames the classes without changing the class structure, so both
  // models must describe the same universe and the same coverage.
  auto short_report = ComputeClassCoverage(short_names, tests, 2);
  auto long_report = ComputeClassCoverage(long_names, tests, 2);
  ASSERT_TRUE(short_report.error.ok()) << short_report.error.message;
  EXPECT_EQ(long_report.total_class_tuples, short_report.total_class_tuples);
  EXPECT_EQ(long_report.covered_class_tuples, short_report.covered_class_tuples);
  EXPECT_DOUBLE_EQ(long_report.coverage_ratio, short_report.coverage_ratio);

  auto [short_ms, long_ms] =
      FastestClassCoverageMsEach(short_names, long_names, tests, kRepetitions);

  // A flat array read touches the same bytes either way, so the honest ratio is
  // 1.0 whatever the labels are, and the bound separates that from a name-keyed
  // hash lookup, which hashes and compares two orders of magnitude more bytes
  // per projection under the long labels and lands at 3.4.
  //
  // That is a narrow separation, and worth stating plainly: 3.4 is the entire
  // signal, while heavy parallel load has been seen to carry this ratio to 2.18.
  // The bound is wedged between the two with little room either side, and both
  // sides are real — below it the gate reports on the machine, above 3.4 it
  // reports on nothing. What keeps it usable is the precision of the
  // measurement above, many repetitions with alternating order, rather than the
  // width of the bound. So a failure here is not answered by raising the
  // number, which has nowhere to go: it is answered by a formulation carrying
  // more signal, one whose baseline does not move with the implementation being
  // measured.
  EXPECT_LT(long_ms, 3.0 * short_ms)
      << "long names=" << long_ms << "ms short names=" << short_ms << "ms";
}

TEST(EquivalenceClassTest, PreflightUsesClassUniverseInsteadOfRawValues) {
  constexpr uint32_t kValues = 4001;
  Parameter left{"left", {}};
  Parameter right{"right", {}};
  left.values.reserve(kValues);
  right.values.reserve(kValues);
  std::vector<std::string> same_class(kValues, "one");
  for (uint32_t index = 0; index < kValues; ++index) {
    left.values.push_back("l" + std::to_string(index));
    right.values.push_back("r" + std::to_string(index));
  }
  left.set_equivalence_classes(same_class);
  right.set_equivalence_classes(std::move(same_class));

  auto report = ComputeClassCoverage({left, right}, {{{0, 0}}}, 2);

  EXPECT_TRUE(report.error.ok()) << report.error.message;
  EXPECT_EQ(report.total_class_tuples, 1u);
  EXPECT_EQ(report.covered_class_tuples, 1u);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
}
