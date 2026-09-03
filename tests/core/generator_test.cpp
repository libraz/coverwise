#include "core/generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/constraint_validator.h"
#include "validator/coverage_validator.h"

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

TEST(GeneratorTest, InvalidAndConstraintViolatingSeedsIgnored) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac"}, {}},
      {"browser", {"chrome", "ie"}, {false, true}},
  };
  opts.constraint_expressions = {"IF os=mac THEN browser!=chrome"};
  opts.strength = 2;
  opts.seed = 42;
  opts.seeds = {TestCase{{0, 1}}, TestCase{{1, 0}}};

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  for (const auto& tc : result.tests) {
    EXPECT_NE(tc.values[1], 1u) << "Positive test contains invalid browser=ie";
    EXPECT_FALSE(tc.values[0] == 1u && tc.values[1] == 0u)
        << "Positive test violates os=mac constraint";
  }
  ASSERT_GE(result.warnings.size(), 2u);
  EXPECT_EQ(result.warnings[0], "Seed test 0 ignored: value browser=ie is marked invalid");
  EXPECT_EQ(result.warnings[1], "Seed test 1 ignored: violates a constraint");
}

TEST(GeneratorTest, DropsSeedsBeyondMaxTests) {
  GenerateOptions opts;
  opts.parameters = {
      {"a", {"0", "1"}, {}},
      {"b", {"0", "1"}, {}},
  };
  opts.max_tests = 1;
  opts.seeds = {TestCase{{0, 0}}, TestCase{{1, 1}}};

  auto result = Generate(opts);

  ASSERT_EQ(result.tests.size(), 1u);
  EXPECT_EQ(result.tests[0].values, (std::vector<uint32_t>{0, 0}));
  EXPECT_NE(std::find(result.warnings.begin(), result.warnings.end(),
                      "Seed test count (2) exceeds maxTests (1); some seeds were dropped"),
            result.warnings.end());
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

TEST(GeneratorTest, ConstraintsAndSubModelsCombined) {
  GenerateOptions opts;
  opts.parameters = {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari"}, {}},
      {"db", {"mysql", "postgres"}, {}},
      {"size", {"small", "large"}, {}},
  };
  opts.constraint_expressions = {"IF os=mac THEN browser!=safari"};
  opts.strength = 2;
  opts.seed = 42;

  SubModel sm;
  sm.parameter_names = {"os", "browser", "db"};
  sm.strength = 3;
  opts.sub_models = {sm};

  auto result = Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_TRUE(result.warnings.empty());

  // Verify no test case violates the constraint: os=mac AND browser=safari.
  for (const auto& tc : result.tests) {
    bool is_mac = (tc.values[0] == 1);
    bool is_safari = (tc.values[1] == 2);
    EXPECT_FALSE(is_mac && is_safari) << "Constraint violation: os=mac, browser=safari";
  }

  // Independently validate pairwise coverage using the validator.
  // The validator does not know about constraints, so constraint-excluded tuples
  // (e.g. os=mac,browser=safari) will appear uncovered. Verify that the only
  // uncovered tuples are those involving the constrained combination.
  auto cov_report = coverwise::validator::ValidateCoverage(opts.parameters, result.tests, 2);
  // All non-constrained tuples must be covered. The constraint excludes
  // os=mac + browser=safari, which removes 1 pair from the (os, browser)
  // combination. The remaining pairs among other parameter combos are unaffected.
  // Total pairwise tuples = C(4,2) sums: (3*3)+(3*2)+(3*2)+(3*2)+(3*2)+(2*2) = 9+6+6+6+6+4 = 37.
  // Only os=mac,browser=safari is excluded -> at most 1 tuple may be uncovered.
  EXPECT_LE(cov_report.uncovered.size(), 1u);

  // Independently validate constraints using the constraint validator.
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expr : opts.constraint_expressions) {
    auto parse_result = coverwise::model::ParseConstraint(expr, opts.parameters);
    ASSERT_TRUE(parse_result.error.ok()) << parse_result.error.message;
    constraints.push_back(std::move(parse_result.constraint));
  }
  auto con_report = coverwise::validator::ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(con_report.violations, 0u);
}

namespace {

/// @brief Build the model used by the sub-model diagnostic tests: four
/// three-valued parameters, pairwise, plus a sub-model that repeats the same
/// strength over three of them so both engines enumerate the same pairs.
GenerateOptions OverlappingSubModelOptions() {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "a2"}, {}},
      {"B", {"b0", "b1", "b2"}, {}},
      {"C", {"c0", "c1", "c2"}, {}},
      {"D", {"d0", "d1", "d2"}, {}},
  };
  opts.strength = 2;
  opts.seed = 42;

  SubModel sm;
  sm.parameter_names = {"A", "B", "C"};
  sm.strength = 2;
  opts.sub_models = {sm};
  return opts;
}

/// @brief Number of distinct (parameter index, value index) tuples in the list.
size_t DistinctTupleCount(const std::vector<coverwise::model::UncoveredTuple>& uncovered) {
  std::set<std::vector<std::pair<uint32_t, uint32_t>>> seen;
  for (const auto& ut : uncovered) {
    seen.insert(ut.indices);
  }
  return seen.size();
}

}  // namespace

TEST(GeneratorTest, OverlappingSubModelCountsEachUncoveredTupleOnce) {
  GenerateOptions opts = OverlappingSubModelOptions();
  opts.max_tests = 3;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  ASSERT_LT(result.coverage, 1.0);

  // The engines together report more shortfall than the model actually has:
  // stats sums the engines, while uncovered_count describes their union.
  uint64_t summed_shortfall = result.stats.total_tuples - result.stats.covered_tuples;
  EXPECT_LT(result.uncovered_count, summed_shortfall);

  // Every distinct tuple fits in the diagnostic budget for a model this small,
  // so the list is exactly the union and nothing is omitted.
  EXPECT_EQ(result.uncovered.size(), result.uncovered_count);
  EXPECT_EQ(DistinctTupleCount(result.uncovered), result.uncovered.size());
  EXPECT_EQ(result.omitted_uncovered, 0u);

  // 6 parameter pairs x 9 value pairs, less the 6 pairs each of the 3 tests covers.
  EXPECT_EQ(result.uncovered_count, 36u);
  EXPECT_EQ(summed_shortfall, 54u);

  // One suggestion per distinct tuple; the same test is never proposed twice.
  std::set<std::string> descriptions;
  for (const auto& suggestion : result.suggestions) {
    EXPECT_TRUE(descriptions.insert(suggestion.description).second)
        << "duplicate suggestion: " << suggestion.description;
  }
  EXPECT_EQ(result.suggestions.size(), result.uncovered.size());
}

TEST(GeneratorTest, OverlappingSubModelReportsNoShortfallWhenComplete) {
  GenerateOptions opts = OverlappingSubModelOptions();

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_EQ(result.uncovered_count, 0u);
  EXPECT_EQ(result.omitted_uncovered, 0u);
  EXPECT_TRUE(result.suggestions.empty());
}

TEST(GeneratorTest, OverlappingSubModelSpendsDiagnosticBudgetOnDistinctTuples) {
  GenerateOptions opts;
  for (char name = 'A'; name <= 'I'; ++name) {
    opts.parameters.push_back({std::string(1, name), {"0", "1", "2", "3", "4", "5"}, {}});
  }
  opts.strength = 2;
  opts.seed = 7;
  opts.max_tests = 1;

  SubModel sm;
  for (char name = 'A'; name <= 'H'; ++name) {
    sm.parameter_names.push_back(std::string(1, name));
  }
  sm.strength = 2;
  opts.sub_models = {sm};

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  // C(9,2) * 36 pairs, less the 36 the single test covers.
  EXPECT_EQ(result.uncovered_count, 1260u);
  EXPECT_LT(result.uncovered_count, result.stats.total_tuples - result.stats.covered_tuples);

  // The truncated list is full and holds no repeats, so the budget was spent
  // entirely on interactions the user has not seen yet.
  ASSERT_EQ(result.uncovered.size(), coverwise::model::kMaxDiagnosticTuples);
  EXPECT_EQ(DistinctTupleCount(result.uncovered), result.uncovered.size());
  EXPECT_EQ(result.omitted_uncovered, result.uncovered_count - result.uncovered.size());
}

TEST(GeneratorTest, ClassCoverageUsesGenerationConstraints) {
  GenerateOptions opts;
  Parameter a("A", {"a0", "a1"});
  a.set_equivalence_classes({"c0", "c1"});
  Parameter b("B", {"b0", "b1"});
  b.set_equivalence_classes({"d0", "d1"});
  opts.parameters = {a, b};
  opts.constraint_expressions = {"IF A=a1 THEN B=b1"};
  opts.strength = 2;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  ASSERT_TRUE(result.class_coverage.has_value());
  EXPECT_EQ(result.class_coverage->total_class_tuples, 3u);
  EXPECT_EQ(result.class_coverage->covered_class_tuples, 3u);
  EXPECT_DOUBLE_EQ(result.class_coverage->class_coverage_ratio, 1.0);
}

TEST(GeneratorTest, BoundaryExpansionKeepsPerValueMetadata) {
  GenerateOptions opts;
  Parameter n("n", {"5"});
  n.set_aliases({{"five"}});
  n.set_equivalence_classes({"mid"});
  Parameter os("os", {"win", "mac"});
  os.set_equivalence_classes({"desktop", "laptop"});
  opts.parameters = {n, os};
  opts.boundary_configs["n"] = {coverwise::model::BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
  opts.constraint_expressions = {"IF n=five THEN os!=mac"};
  opts.strength = 2;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok()) << result.error.message;
  const auto& expanded = result.parameters[0];
  EXPECT_EQ(expanded.values, (std::vector<std::string>{"3", "4", "5", "6", "7"}));
  const uint32_t five = coverwise::model::ResolveValueName(expanded, "5");
  ASSERT_NE(five, coverwise::model::kUnassigned);
  EXPECT_EQ(expanded.aliases(five), (std::vector<std::string>{"five"}));
  EXPECT_EQ(expanded.equivalence_class(five), "mid");
  // Values the range generated have no metadata of their own.
  const uint32_t three = coverwise::model::ResolveValueName(expanded, "3");
  ASSERT_NE(three, coverwise::model::kUnassigned);
  EXPECT_TRUE(expanded.aliases(three).empty());
  EXPECT_TRUE(expanded.equivalence_class(three).empty());
  // The alias still resolves, so a constraint written against it parses.
  EXPECT_EQ(coverwise::model::ResolveValueName(expanded, "five"), five);
  ASSERT_TRUE(result.class_coverage.has_value());
  EXPECT_GT(result.class_coverage->total_class_tuples, 0u);
}

TEST(GeneratorTest, RejectsValueSetThatExpansionMakesAmbiguous) {
  GenerateOptions opts;
  Parameter n("n", {"10"});
  n.set_aliases({{"5"}});
  opts.parameters = {n, Parameter("os", {"win", "mac"})};
  opts.boundary_configs["n"] = {coverwise::model::BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
  opts.strength = 2;

  auto result = Generate(opts);

  // '5' is unambiguous before expansion and collides with a generated value
  // after it, so the collection is judged again on the expanded value space.
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_EQ(result.error.message, "Ambiguous value or alias '5' in parameter 'n'");
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

TEST(GeneratorTest, NegativeTestingCoversRequestedThreeWiseTuples) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "bad"}, {false, true}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1", "c2"}, {}},
  };
  opts.strength = 3;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  std::set<std::pair<uint32_t, uint32_t>> covered;
  for (const auto& test : result.negative_tests) {
    if (test.values[0] == 1) covered.emplace(test.values[1], test.values[2]);
  }
  EXPECT_EQ(covered.size(), 6u);
  EXPECT_TRUE(result.warnings.empty());
}

TEST(GeneratorTest, NegativeTestingDeterministicallyCompletesFourWiseCoverage) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "bad"}, {false, true}},
      {"B", {"b0", "b1", "b2"}, {}},
      {"C", {"c0", "c1", "c2"}, {}},
      {"D", {"d0", "d1", "d2"}, {}},
  };
  opts.strength = 4;

  for (uint32_t seed : {0u, 1u, 42u, 999u}) {
    opts.seed = seed;
    auto result = Generate(opts);
    ASSERT_TRUE(result.error.ok());
    ASSERT_TRUE(result.negative_coverage.has_value());
    EXPECT_EQ(result.negative_tests.size(), 27u);
    EXPECT_EQ(result.negative_coverage->total_tuples, 27u);
    EXPECT_EQ(result.negative_coverage->covered_tuples, 27u);
    EXPECT_EQ(result.negative_coverage->omitted_tuples, 0u);
    EXPECT_DOUBLE_EQ(result.negative_coverage->coverage_ratio, 1.0);
  }
}

TEST(GeneratorTest, MaxTestsCapsCombinedPositiveAndNegativeSuite) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "bad"}, {false, true}},
      {"B", {"b0", "b1"}, {}},
  };
  opts.strength = 2;
  opts.max_tests = 3;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  ASSERT_TRUE(result.negative_coverage.has_value());
  EXPECT_EQ(result.tests.size(), 2u);
  EXPECT_EQ(result.negative_tests.size(), 1u);
  EXPECT_EQ(result.stats.test_count, 3u);
  EXPECT_EQ(result.negative_coverage->total_tuples, 2u);
  EXPECT_EQ(result.negative_coverage->covered_tuples, 1u);
  EXPECT_EQ(result.negative_coverage->omitted_tuples, 1u);
  EXPECT_NE(std::find(result.warnings.begin(), result.warnings.end(),
                      "Negative generation stopped at maxTests (3) before reaching full coverage"),
            result.warnings.end());
}

TEST(GeneratorTest, NegativeTestingSingleParameterProducesOneExample) {
  GenerateOptions opts;
  opts.parameters = {{"A", {"valid", "bad"}, {false, true}}};
  opts.strength = 1;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok());
  ASSERT_EQ(result.negative_tests.size(), 1u);
  EXPECT_EQ(result.negative_tests[0].values, (std::vector<uint32_t>{1}));
}

TEST(GeneratorTest, NegativeTestingWarnsWhenInvalidValueCannotSatisfyConstraints) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"valid", "bad"}, {false, true}},
      {"B", {"b0", "b1"}, {}},
  };
  opts.strength = 2;
  opts.constraint_expressions = {"A!=bad"};

  auto result = Generate(opts);

  EXPECT_TRUE(result.negative_tests.empty());
  // Nothing containing A=bad is feasible, so there is no shortfall to report:
  // the warning states that fact instead of claiming an unmet target. The
  // metrics say the same thing, which is what makes the two readable together.
  EXPECT_NE(std::find(result.warnings.begin(), result.warnings.end(),
                      "No feasible negative coverage target for A=bad"),
            result.warnings.end());
  EXPECT_EQ(std::find(result.warnings.begin(), result.warnings.end(),
                      "Negative coverage incomplete for A=bad"),
            result.warnings.end());
  ASSERT_TRUE(result.negative_coverage.has_value());
  EXPECT_EQ(result.negative_coverage->total_tuples, 0u);
  EXPECT_EQ(result.negative_coverage->omitted_tuples, 0u);
}

// The negative metrics and the warnings are meant to be read against each
// other, so no warning may claim a shortfall the metrics do not show.
TEST(GeneratorTest, NoNegativeWarningClaimsAShortfallTheMetricsDoNotShow) {
  auto claims_incompleteness = [](const std::vector<std::string>& warnings) {
    for (const auto& warning : warnings) {
      if (warning.rfind("Negative coverage incomplete for ", 0) == 0) return true;
    }
    return false;
  };

  {
    // Every single-fault combination for the invalid value is forbidden.
    GenerateOptions opts;
    opts.parameters = {
        {"A", {"valid", "bad"}, {false, true}},
        {"B", {"b0", "b1"}, {}},
    };
    opts.strength = 2;
    opts.constraint_expressions = {"A!=bad"};

    auto result = Generate(opts);

    ASSERT_TRUE(result.negative_coverage.has_value());
    EXPECT_EQ(result.negative_coverage->omitted_tuples, 0u);
    EXPECT_DOUBLE_EQ(result.negative_coverage->coverage_ratio, 1.0);
    EXPECT_FALSE(claims_incompleteness(result.warnings));
  }
  {
    // maxTests stops negative generation with feasible targets still uncovered.
    GenerateOptions opts;
    opts.parameters = {
        {"A", {"valid", "bad"}, {false, true}},
        {"B", {"b0", "b1", "b2"}, {}},
    };
    opts.strength = 2;
    opts.max_tests = 2;

    auto result = Generate(opts);

    ASSERT_TRUE(result.negative_coverage.has_value());
    ASSERT_TRUE(claims_incompleteness(result.warnings));
    EXPECT_GT(result.negative_coverage->omitted_tuples, 0u);
    EXPECT_LT(result.negative_coverage->coverage_ratio, 1.0);
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
// Constraint integrity under over-constrained models (B-3)
// ---------------------------------------------------------------------------

/// @brief Validate every generated positive test against the constraints.
/// @return Number of constraint violations across all positive tests.
uint32_t CountPositiveViolations(const coverwise::model::GenerateResult& result,
                                 const GenerateOptions& opts) {
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expr : opts.constraint_expressions) {
    auto parse_result = coverwise::model::ParseConstraint(expr, opts.parameters);
    if (!parse_result.error.ok()) return UINT32_MAX;
    constraints.push_back(std::move(parse_result.constraint));
  }
  auto report = coverwise::validator::ValidateConstraints(result.tests, constraints);
  return report.violations;
}

TEST(GeneratorConstraintIntegrityTest, OverConstrainedModelEmitsNoViolatingTests) {
  // A model where, for some partial assignments, every value of a later
  // parameter is constraint-pruned. The previous greedy fallback would emit a
  // constraint-violating value as a "last resort"; the fix must instead drop
  // the failed construction so the output contains zero violations.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  // When A=a0 and B=b1 (or A=a1 and B=b0), C has no satisfying value.
  opts.constraint_expressions = {
      "IF A=a0 THEN C!=c0",
      "IF B=b1 THEN C!=c1",
      "IF A=a1 THEN C!=c1",
      "IF B=b0 THEN C!=c0",
  };
  opts.strength = 2;

  // Try many seeds to exercise different parameter orderings in the greedy loop.
  for (uint64_t seed = 0; seed < 25; ++seed) {
    opts.seed = seed;
    auto result = Generate(opts);
    EXPECT_EQ(CountPositiveViolations(result, opts), 0u)
        << "Constraint-violating test emitted at seed " << seed;
  }
}

TEST(GeneratorConstraintIntegrityTest, FullyUnsatisfiableParameterEmitsNoViolatingTests) {
  // C can never be assigned: every value is rejected unconditionally. Every
  // C-tuple is excluded as inherently invalid, so the generator emits zero
  // positive tests rather than a constraint-violating one.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  opts.constraint_expressions = {"C!=c0", "C!=c1"};
  opts.strength = 2;
  opts.seed = 7;

  auto result = Generate(opts);

  EXPECT_EQ(CountPositiveViolations(result, opts), 0u);
  EXPECT_TRUE(result.tests.empty());
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(result.error.message, "Constraints are unsatisfiable");
}

TEST(GeneratorConstraintIntegrityTest, AllInvalidParameterIsRejected) {
  GenerateOptions opts;
  opts.parameters = {
      Parameter{"A", {"a0", "a1"}, {true, true}},
      Parameter{"B", {"b0", "b1"}, {}},
  };
  opts.strength = 2;

  auto result = Generate(opts);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_EQ(result.error.message, "Parameter 'A' must have at least one valid value");
  EXPECT_TRUE(result.tests.empty());
}

// ---------------------------------------------------------------------------
// Machine-readable error signal and coverage reporting (B-1)
// ---------------------------------------------------------------------------

TEST(GeneratorErrorSignalTest, ConstraintParseErrorSetsConstraintErrorCode) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  // References a parameter that does not exist -> constraint parse error.
  opts.constraint_expressions = {"IF nonexistent=x THEN B!=b0"};
  opts.strength = 2;

  auto result = Generate(opts);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_TRUE(result.tests.empty());
}

TEST(GeneratorErrorSignalTest, SuccessfulGenerationLeavesErrorOk) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 1;

  auto result = Generate(opts);

  EXPECT_TRUE(result.error.ok());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
}

TEST(GeneratorSemanticValidationTest, RejectsMetadataLengthMismatch) {
  GenerateOptions opts;
  Parameter param{"A", {"0", "1"}, {}};
  param.set_invalid({false});
  opts.parameters = {param, {"B", {"0", "1"}, {}}};
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
}

TEST(GeneratorSemanticValidationTest, RejectsDuplicateSubModelParameter) {
  GenerateOptions opts;
  opts.parameters = {{"A", {"0", "1"}, {}}, {"B", {"0", "1"}, {}}};
  opts.sub_models = {{{"A", "A"}, 2}};
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
}

TEST(GeneratorSemanticValidationTest, RejectsSeedOutsideUint32Domain) {
  GenerateOptions opts;
  opts.parameters = {{"A", {"0", "1"}, {}}, {"B", {"0", "1"}, {}}};
  opts.seed = static_cast<uint64_t>(UINT32_MAX) + 1;
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
}

TEST(GeneratorSemanticValidationTest, RejectsNonFiniteWeight) {
  GenerateOptions opts;
  opts.parameters = {{"A", {"0", "1"}, {}}, {"B", {"0", "1"}, {}}};
  opts.weights.entries["A"]["0"] = std::numeric_limits<double>::infinity();
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
}

TEST(GeneratorSemanticValidationTest, EstimateReportsStructuredError) {
  GenerateOptions opts;
  opts.parameters = {{"A", {"0", "1"}, {}}};
  opts.strength = 2;
  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kInvalidInput);
}

TEST(GeneratorErrorSignalTest, IncompleteCoverageReportsBelowOneWithoutError) {
  // Insufficient coverage (without a constraint parse error) must be reported
  // via coverage < 1.0, not via the error signal. The CLI maps this to exit 2.
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "a2", "a3"}, {}},
      {"B", {"b0", "b1", "b2", "b3"}, {}},
      {"C", {"c0", "c1", "c2", "c3"}, {}},
  };
  opts.strength = 2;
  opts.seed = 3;
  opts.max_tests = 3;  // Too few to cover all 48 pairwise tuples.

  auto result = Generate(opts);

  EXPECT_TRUE(result.error.ok());
  EXPECT_LT(result.coverage, 1.0);
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

TEST(GeneratorExtendTest, StrictPreservesInvalidExistingPrefixAndExcludesItFromCoverage) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.constraint_expressions = {"IF A=1 THEN B=1"};

  std::vector<TestCase> existing = {
      TestCase{{1, 0}},   // Constraint violation.
      TestCase{{0}},      // Missing column.
      TestCase{{0, 99}},  // Out-of-range value.
  };
  auto result = Extend(existing, opts, ExtendMode::kStrict);

  ASSERT_TRUE(result.error.ok());
  ASSERT_GE(result.tests.size(), existing.size());
  for (size_t i = 0; i < existing.size(); ++i) {
    EXPECT_EQ(result.tests[i].values, existing[i].values);
  }
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_EQ(result.stats.covered_tuples, result.stats.total_tuples);
  EXPECT_EQ(std::count_if(result.warnings.begin(), result.warnings.end(),
                          [](const auto& warning) {
                            return warning.find("preserved but excluded from coverage") !=
                                   std::string::npos;
                          }),
            3);
}

TEST(GeneratorExtendTest, StrictKeepsAdditionalSeedsAfterExistingPrefix) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.seeds = {TestCase{{1, 1}}};

  auto result = Extend({TestCase{{0, 0}}}, opts, ExtendMode::kStrict);

  ASSERT_TRUE(result.error.ok());
  ASSERT_GE(result.tests.size(), 2u);
  EXPECT_EQ(result.tests[0].values, (std::vector<uint32_t>{0, 0}));
  EXPECT_EQ(result.tests[1].values, (std::vector<uint32_t>{1, 1}));
}

TEST(GeneratorExtendTest, RejectsMaxTestsBelowExistingCount) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.max_tests = 1;

  auto result = Extend({TestCase{{0, 0}}, TestCase{{1, 1}}}, opts, ExtendMode::kStrict);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_NE(result.error.message.find("existing test count"), std::string::npos);
  EXPECT_TRUE(result.tests.empty());
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

TEST(EstimateModelTest, RejectsMalformedConstraintLikeGenerate) {
  GenerateOptions opts;
  opts.parameters = {{"A", {"0", "1"}, {}}, {"B", {"0", "1"}, {}}};
  opts.constraint_expressions = {"unknown = 0"};

  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_NE(stats.error.message.find("unknown"), std::string::npos);
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
  sm.parameter_names = {"A", "B", "C"};
  sm.strength = 3;
  opts.sub_models = {sm};

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.parameter_count, 3u);
  EXPECT_EQ(stats.sub_model_count, 1u);
  // Global pairwise: 12. Three-wise sub-model: 8. Raw total: 20.
  EXPECT_EQ(stats.total_tuples, 20u);

  auto generated = Generate(opts);
  ASSERT_TRUE(generated.error.ok());
  EXPECT_EQ(generated.stats.total_tuples, stats.total_tuples);
}

TEST(EstimateModelTest, CombinedSubModelBudgetReturnsTupleExplosion) {
  std::vector<std::string> values;
  values.reserve(3000);
  for (int i = 0; i < 3000; ++i) values.push_back(std::to_string(i));

  GenerateOptions opts;
  opts.parameters = {{"A", values}, {"B", values}};
  opts.strength = 2;
  opts.sub_models = {{{"A", "B"}, 2}};

  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_NE(stats.error.message.find("Combined global and sub-model"), std::string::npos);

  auto generated = Generate(opts);
  EXPECT_EQ(generated.error.code, coverwise::model::Error::Code::kTupleExplosion);
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
// Cross-surface parity tests (warning strings and estimate clamping).
// The expected literals below are pinned and must stay byte-identical to the
// strings asserted in src/ts/core/generator.test.ts.
// ---------------------------------------------------------------------------

// Incomplete coverage caused by the maxTests cap must emit the canonical
// max-tests warning string.
TEST(GeneratorParityTest, IncompleteCoverageMaxTestsWarning) {
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

  EXPECT_LT(result.coverage, 1.0);
  EXPECT_NE(std::find(result.warnings.begin(), result.warnings.end(),
                      "Generation stopped at maxTests (3) before reaching 100% coverage"),
            result.warnings.end());
}

// Tuples that cannot be extended to a complete satisfying assignment are not
// part of the required coverage universe.
TEST(GeneratorParityTest, UnreachableTupleIsExcludedFromUniverse) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.seed = 1;
  // The constraints interact to make (A=0, B=1) impossible to complete.
  opts.constraint_expressions = {
      "IF A=0 AND B=1 THEN C!=0",
      "IF A=0 AND B=1 THEN C!=1",
  };

  auto result = Generate(opts);

  EXPECT_TRUE(result.error.ok());
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_TRUE(result.warnings.empty());
}

// estimateModel rejects a degenerate product above the allocation budget.
TEST(GeneratorParityTest, EstimateDegenerateReturnsTupleExplosion) {
  GenerateOptions opts;
  std::vector<std::string> values;
  for (uint32_t i = 0; i < 1000; ++i) values.push_back(std::to_string(i));
  opts.parameters = {
      {"A", values, {}},
      {"B", values, {}},
      {"C", values, {}},
      {"D", values, {}},
  };
  opts.strength = 4;  // parameterCount == strength -> product path.

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_EQ(stats.estimated_tests, 0u);
}

// estimateModel rejects a large non-degenerate model before generation.
TEST(GeneratorParityTest, EstimateLargeModelReturnsTupleExplosion) {
  GenerateOptions opts;
  std::vector<std::string> values;
  for (uint32_t i = 0; i < 1000; ++i) values.push_back(std::to_string(i));
  for (int i = 0; i < 5; ++i) {
    opts.parameters.push_back({"p" + std::to_string(i), values, {}});
  }
  opts.strength = 3;  // parameterCount(5) > strength(3) -> estimate path.

  auto stats = EstimateModel(opts);

  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_EQ(stats.estimated_tests, 0u);
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

TEST(GeneratorEdgeCaseTest, EmptyParametersAreInvalid) {
  GenerateOptions opts;
  opts.strength = 2;
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_TRUE(result.tests.empty());
  EXPECT_EQ(result.stats.total_tuples, 0u);
}

TEST(GeneratorEdgeCaseTest, StrengthAboveSingleParameterIsInvalid) {
  GenerateOptions opts;
  opts.parameters = {{"os", {"win", "mac", "linux"}, {}}};
  opts.strength = 2;
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_EQ(result.stats.total_tuples, 0u);
}

TEST(GeneratorEdgeCaseTest, StrengthZeroIsInvalid) {
  GenerateOptions opts;
  opts.parameters = {
      {"a", {"1", "2", "3"}, {}},
      {"b", {"x", "y", "z"}, {}},
      {"c", {"p", "q", "r"}, {}},
  };
  opts.strength = 0;
  opts.seed = 42;
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_TRUE(result.tests.empty());
  EXPECT_EQ(result.stats.total_tuples, 0u);
}

TEST(GeneratorEdgeCaseTest, StrengthExceedsParamCountIsInvalid) {
  GenerateOptions opts;
  opts.parameters = {{"a", {"1", "2"}, {}}, {"b", {"1", "2"}, {}}};
  opts.strength = 5;
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
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
  ASSERT_FALSE(result.suggestions.empty());
  EXPECT_EQ(result.suggestions[0].test_case.values.size(), opts.parameters.size());
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
  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kInvalidInput);
}

// Edge: EstimateModel with 1 parameter
TEST(EstimateModelEdgeTest, SingleParameter) {
  GenerateOptions opts;
  opts.parameters = {{"os", {"win", "mac", "linux"}, {}}};
  opts.strength = 2;
  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.parameter_count, 0u);
  EXPECT_EQ(stats.total_tuples, 0u);
  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kInvalidInput);
}

// Edge: EstimateModel rejects a raw tuple upper bound above the safe budget.
TEST(EstimateModelEdgeTest, LargeValuesReturnTupleExplosion) {
  GenerateOptions opts;
  // 10 params × 100 values at strength 3 → 100^3 = 1M, should not overflow
  for (int i = 0; i < 10; i++) {
    std::vector<std::string> vals;
    for (int j = 0; j < 100; j++) vals.push_back(std::to_string(j));
    opts.parameters.push_back({"p" + std::to_string(i), std::move(vals), {}});
  }
  opts.strength = 3;
  auto stats = EstimateModel(opts);
  EXPECT_EQ(stats.error.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_EQ(stats.estimated_tests, 0u);
}

// ---------------------------------------------------------------------------
// Completion phase: coverage completeness at hard endpoints
// ---------------------------------------------------------------------------

// Regression: greedy construction alone stalls at strength == parameter count
// and used to finish below 100%. The deterministic completion phase must close
// every feasible tuple, for every seed.
TEST(GenerateCompletionTest, ReachesFullCoverageAtStrengthEqualsParamCount) {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    GenerateOptions opts;
    opts.parameters.push_back({"A", {"a0", "a1", "a2", "a3"}, {}});
    opts.parameters.push_back({"B", {"b0", "b1", "b2", "b3"}, {}});
    opts.parameters.push_back({"C", {"c0", "c1", "c2", "c3"}, {}});
    opts.parameters.push_back({"D", {"d0", "d1", "d2", "d3"}, {}});
    opts.strength = 4;
    opts.seed = seed;
    auto result = Generate(opts);
    EXPECT_DOUBLE_EQ(result.coverage, 1.0) << "seed=" << seed;
    EXPECT_TRUE(result.uncovered.empty()) << "seed=" << seed;
    // t == n is the full cross product: exactly 4^4 distinct tests.
    EXPECT_EQ(result.tests.size(), 256u) << "seed=" << seed;
    auto cov = coverwise::validator::ValidateCoverage(opts.parameters, result.tests, 4);
    EXPECT_DOUBLE_EQ(cov.coverage_ratio, 1.0) << "seed=" << seed;
  }
}

// Regression: high strength on a mixed model must also reach full coverage.
TEST(GenerateCompletionTest, ReachesFullCoverageForHighStrengthMixedModel) {
  GenerateOptions opts;
  opts.parameters.push_back({"A", {"a0", "a1", "a2"}, {}});
  opts.parameters.push_back({"B", {"b0", "b1", "b2"}, {}});
  opts.parameters.push_back({"C", {"c0", "c1"}, {}});
  opts.parameters.push_back({"D", {"d0", "d1", "d2"}, {}});
  opts.parameters.push_back({"E", {"e0", "e1"}, {}});
  opts.strength = 4;
  opts.seed = 3;
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
}

// Integer boundary endpoints are accepted/rejected on the JS safe-integer rule
// (|v| <= 2^53-1), matching Number.isSafeInteger on the TypeScript surfaces so
// native/WASM and pure-TS agree on the same model.
TEST(GenerateBoundaryValidationTest, IntegerEndpointsUseSafeIntegerRule) {
  auto make_opts = [](double max_value) {
    GenerateOptions opts;
    coverwise::model::Parameter p;
    p.name = "n";  // Empty values: the boundary range supplies the value set.
    opts.parameters.push_back(p);
    opts.parameters.push_back({"other", {"a", "b"}, {}});
    opts.strength = 2;
    coverwise::model::BoundaryConfig bc;
    bc.type = coverwise::model::BoundaryConfig::Type::kInteger;
    bc.min_value = 0;
    bc.max_value = max_value;
    opts.boundary_configs["n"] = bc;
    return opts;
  };

  // Within the safe-integer range: accepted.
  auto ok = Generate(make_opts(1000.0));
  EXPECT_EQ(ok.error.code, coverwise::model::Error::Code::kOk);

  // Beyond 2^53: rejected as invalid input (previously accepted under int64).
  auto bad = Generate(make_opts(1e18));
  EXPECT_EQ(bad.error.code, coverwise::model::Error::Code::kInvalidInput);
}

// A hard contradictory (pigeonhole) model must terminate with a constraint
// error in finite time rather than hanging on unbounded backtracking.
TEST(GenerateCompletionTest, TerminatesOnHardContradictoryModel) {
  GenerateOptions opts;
  const int n = 12;
  for (int i = 0; i < n; ++i) {
    opts.parameters.push_back({"p" + std::to_string(i), {"0", "1"}, {}});
  }
  for (int i = 0; i < n; ++i) {
    for (int j = i + 1; j < n; ++j) {
      opts.constraint_expressions.push_back("IF p" + std::to_string(i) + "=0 THEN p" +
                                            std::to_string(j) + "!=0");
      opts.constraint_expressions.push_back("IF p" + std::to_string(i) + "=1 THEN p" +
                                            std::to_string(j) + "!=1");
    }
  }
  opts.strength = 2;
  auto result = Generate(opts);
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_TRUE(result.tests.empty());
}

// Constraints that stall greedy must still reach full coverage with zero
// violations in the output suite.
TEST(GenerateCompletionTest, ReachesFullCoverageUnderStallingConstraints) {
  GenerateOptions opts;
  opts.parameters.push_back({"os", {"win", "mac", "linux"}, {}});
  opts.parameters.push_back({"browser", {"chrome", "safari", "edge"}, {}});
  opts.parameters.push_back({"arch", {"x86", "arm"}, {}});
  opts.strength = 2;
  opts.seed = 5;
  opts.constraint_expressions = {"IF os=mac THEN browser!=edge", "IF os=win THEN browser!=safari"};
  auto result = Generate(opts);
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());

  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expr : opts.constraint_expressions) {
    auto parsed = coverwise::model::ParseConstraint(expr, opts.parameters);
    ASSERT_TRUE(parsed.error.ok()) << parsed.error.message;
    constraints.push_back(std::move(parsed.constraint));
  }
  auto con_report = coverwise::validator::ValidateConstraints(result.tests, constraints);
  EXPECT_EQ(con_report.violations, 0u);
}

// ---------------------------------------------------------------------------
// Negative coverage reporting on error paths
// ---------------------------------------------------------------------------

namespace {

/// @brief Model whose negative pass cannot classify its targets in budget.
///
/// P0 carries the only invalid value, and the pairwise-different constraints
/// over P1..P11 activate only while P0 holds it. Positive generation never
/// assigns an invalid value, so it sees the implications as vacuously true; the
/// negative pass pins P0 to "bad" and has to prove eleven parameters cannot take
/// ten distinct values, which costs more nodes than the search budget allows.
GenerateOptions MakeUnclassifiableNegativeModel() {
  constexpr uint32_t kHoles = 10;
  constexpr uint32_t kPigeons = 11;
  std::vector<std::string> holes;
  for (uint32_t index = 0; index < kHoles; ++index) holes.push_back("h" + std::to_string(index));

  GenerateOptions opts;
  std::vector<std::string> fixed_values = holes;
  fixed_values.push_back("bad");
  std::vector<bool> fixed_invalid(kHoles, false);
  fixed_invalid.push_back(true);
  opts.parameters.push_back({"P0", fixed_values, fixed_invalid});
  for (uint32_t index = 1; index <= kPigeons; ++index) {
    opts.parameters.push_back({"P" + std::to_string(index), holes, {}});
  }
  for (uint32_t left = 1; left <= kPigeons; ++left) {
    for (uint32_t right = left + 1; right <= kPigeons; ++right) {
      opts.constraint_expressions.push_back("IF P0 = \"bad\" THEN P" + std::to_string(left) +
                                            " != P" + std::to_string(right));
    }
  }
  opts.strength = 2;
  opts.seed = 42;
  return opts;
}

}  // namespace

TEST(GeneratorTest, NegativeCoverageIsUnsetWhenTargetsCannotBeClassified) {
  auto result = Generate(MakeUnclassifiableNegativeModel());

  ASSERT_FALSE(result.error.ok());
  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kConstraintError);
  EXPECT_EQ(result.error.message, "Constraint search budget exceeded");
  EXPECT_EQ(result.error.detail,
            "Negative coverage targets could not be classified within the search budget");
  // The pass stopped before it could work out omitted tuples and the ratio, so
  // publishing its counters would describe a tuple universe that was never
  // finished being classified.
  EXPECT_FALSE(result.negative_coverage.has_value());
}

TEST(GeneratorTest, NegativeCoverageCountsAgreeOnEveryCompletedPass) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"a0", "a1", "bad"}, {false, false, true}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1", "worse"}, {false, false, true}},
  };
  opts.strength = 2;
  opts.seed = 42;

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_TRUE(result.negative_coverage.has_value());
  const auto& negative = *result.negative_coverage;
  EXPECT_LE(negative.covered_tuples, negative.total_tuples);
  EXPECT_EQ(negative.omitted_tuples, negative.total_tuples - negative.covered_tuples);
  EXPECT_DOUBLE_EQ(negative.coverage_ratio, negative.total_tuples == 0
                                                ? 1.0
                                                : static_cast<double>(negative.covered_tuples) /
                                                      static_cast<double>(negative.total_tuples));
}

TEST(GeneratorTest, AWarningForAnErrorWithoutADetailEndsWithoutASeparator) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  // An unterminated string literal is reported with no detail, so the warning
  // must not carry the separator that would introduce one.
  opts.constraint_expressions = {"A = \"0"};

  auto result = Generate(opts);

  ASSERT_FALSE(result.error.ok());
  ASSERT_TRUE(result.error.detail.empty()) << result.error.detail;
  ASSERT_FALSE(result.warnings.empty());
  EXPECT_EQ(result.warnings[0], result.error.message);
}

TEST(GeneratorTest, AWarningForAnErrorWithADetailKeepsIt) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  opts.constraint_expressions = {"A = 0", "A != 0"};

  auto result = Generate(opts);

  ASSERT_FALSE(result.error.ok());
  ASSERT_FALSE(result.error.detail.empty());
  ASSERT_FALSE(result.warnings.empty());
  EXPECT_EQ(result.warnings[0], result.error.message + ": " + result.error.detail);
}

// ---------------------------------------------------------------------------
// Acceptance at the engine entry
// ---------------------------------------------------------------------------

namespace {

using coverwise::model::BoundaryConfig;
using coverwise::model::Error;

/// @brief The code the model gate answers with for these options.
Error::Code GateCode(GenerateOptions options) {
  auto accepted =
      coverwise::model::AcceptOptions(std::move(options), coverwise::model::ChargedText::None());
  return accepted.ok() ? Error::Code::kOk : accepted.error().code;
}

/// @brief A model whose weights name a value only boundary expansion produces.
GenerateOptions BoundaryWeightModel() {
  GenerateOptions opts;
  opts.parameters = {Parameter("age", {"50"}), Parameter("plan", {"free", "pro"})};
  opts.boundary_configs["age"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 0.0, 10.0, 1.0};
  // Expansion around [0, 10] puts "1" in the value list; the declared list does not.
  opts.weights.entries["age"]["1"] = 3.0;
  opts.strength = 2;
  return opts;
}

}  // namespace

// The rules that resolve names and count values are applied to the value space
// generation will use, so a weight naming a generated boundary value is a model
// the engine accepts rather than one it rejects on the declared values.
TEST(GeneratorAcceptanceTest, WeightsMayNameAValueExpansionProduces) {
  const auto opts = BoundaryWeightModel();

  ASSERT_EQ(GateCode(opts), Error::Code::kOk);
  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok()) << result.error.message;
  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_FALSE(result.tests.empty());
}

/// @brief One model, and the code the gate is supposed to answer with for it.
struct AcceptanceCase {
  std::string name;
  Error::Code expected;
  GenerateOptions options;
};

// Every entry point of the engine answers with the code the model gate answers
// with for the same options: acceptance is decided in one place, not once per
// entry point.
//
// Two assertions per row, because two different things can go wrong and one
// assertion cannot tell them apart.
//
// The derived one compares each entry point against an independent second call
// to the gate. Nothing here says what that call should return, so a rule that
// legitimately moves does not make it red, and an entry point that stops
// routing through the gate does.
//
// The stated one anchors the gate itself to the code each row is an example of.
// Without it a rule can move in both the gate and the entry points at once and
// every comparison above stays green — including a move that stops charging
// something, which is the fail-open direction. That is not hypothetical here:
// this table went green through a change that left one path accepting unbounded
// row text, and the anchor is what its TypeScript twin caught it with.
TEST(GeneratorAcceptanceTest, EveryEntryPointReturnsTheModelGateCode) {
  std::vector<AcceptanceCase> cases;
  cases.push_back(
      {"weights on a generated boundary value", Error::Code::kOk, BoundaryWeightModel()});

  {
    auto opts = BoundaryWeightModel();
    opts.weights.entries["age"]["not a value"] = 2.0;
    cases.push_back(
        {"weights on a value nothing produces", Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    auto opts = BoundaryWeightModel();
    opts.boundary_configs["age"].step = 5.0;
    cases.push_back({"integer boundary with a step other than one", Error::Code::kInvalidInput,
                     std::move(opts)});
  }
  {
    // The declared value is an invalid sentinel and the range supplies the rest.
    GenerateOptions opts;
    opts.parameters = {Parameter("age", {"999"}, {true}), Parameter("plan", {"free", "pro"})};
    opts.boundary_configs["age"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 0.0, 10.0, 1.0};
    opts.strength = 2;
    cases.push_back(
        {"boundary parameter declaring only an invalid value", Error::Code::kOk, std::move(opts)});
  }
  {
    // The metadata length disagrees with the declared values, which expansion
    // itself rejects before any later rule sees the parameter.
    GenerateOptions opts;
    opts.parameters = {Parameter("n", {"5"}, {true, false}), Parameter("plan", {"free", "pro"})};
    opts.boundary_configs["n"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
    opts.strength = 2;
    cases.push_back({"boundary parameter with mismatched metadata", Error::Code::kInvalidInput,
                     std::move(opts)});
  }
  {
    // '5' is unambiguous before expansion and collides with a generated value
    // after it, so the collection is judged on the expanded value space.
    GenerateOptions opts;
    Parameter n("n", {"10"});
    n.set_aliases({{"5"}});
    opts.parameters = {n, Parameter("os", {"win", "mac"})};
    opts.boundary_configs["n"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
    opts.strength = 2;
    cases.push_back(
        {"alias that only expansion makes ambiguous", Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    GenerateOptions opts;
    std::vector<std::string> values;
    for (size_t index = 0; index < 20; ++index) {
      values.push_back(std::string(60 * 1024, 'x') + std::to_string(index));
    }
    opts.parameters = {Parameter("wide", std::move(values)), Parameter("plan", {"free", "pro"})};
    opts.strength = 2;
    cases.push_back(
        {"string data beyond the aggregate budget", Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    auto opts = BoundaryWeightModel();
    opts.strength = 0;
    cases.push_back({"strength below one", Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    auto opts = BoundaryWeightModel();
    opts.sub_models.push_back(SubModel{{"nonexistent"}, 1});
    cases.push_back(
        {"sub-model naming an unknown parameter", Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    // The range would supply the values, but per-value metadata has nothing to
    // attach to until it does.
    GenerateOptions opts;
    Parameter n("n", {});
    n.set_invalid({true});
    opts.parameters = {n, Parameter("plan", {"free", "pro"})};
    opts.boundary_configs["n"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 0.0, 10.0, 1.0};
    opts.strength = 2;
    cases.push_back({"boundary parameter carrying metadata but no declared values",
                     Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    // Within the maximum as declared, past it once expansion has run.
    GenerateOptions opts;
    std::vector<std::string> values;
    for (int value = 1; value <= 16381; ++value) values.push_back(std::to_string(value));
    opts.parameters = {Parameter("n", std::move(values)), Parameter("plan", {"free", "pro"})};
    opts.boundary_configs["n"] =
        BoundaryConfig{BoundaryConfig::Type::kInteger, 100000.0, 100001.0, 1.0};
    opts.strength = 2;
    cases.push_back({"expansion pushing the value count past the per-parameter maximum",
                     Error::Code::kInvalidInput, std::move(opts)});
  }
  {
    // A row's members reach the engine as indices, but a member that did not
    // resolve keeps the caller's own text, and that text is charged.
    GenerateOptions opts;
    opts.parameters = {Parameter("a", {"0", "1"}), Parameter("b", {"0", "1"})};
    opts.strength = 2;
    for (size_t row = 0; row < 40; ++row) {
      TestCase recorded;
      recorded.values.assign(2, coverwise::model::kUnassigned);
      recorded.unresolved = {std::string(60 * 1024, 'x'), std::string(60 * 1024, 'y')};
      opts.seeds.push_back(std::move(recorded));
    }
    cases.push_back({"recorded-row text beyond the aggregate budget", Error::Code::kInvalidInput,
                     std::move(opts)});
  }

  const std::vector<TestCase> no_existing;
  for (const auto& [name, expected, opts] : cases) {
    const Error::Code gate = GateCode(opts);
    EXPECT_EQ(gate, expected) << name;
    EXPECT_EQ(Generate(opts).error.code, gate) << name;
    EXPECT_EQ(EstimateModel(opts).error.code, gate) << name;
    EXPECT_EQ(Extend(no_existing, opts, ExtendMode::kStrict).error.code, gate) << name;
  }
}

// Expansion runs before the rest of the acceptance rules, so a failure of the
// expansion itself is the answer the caller gets rather than a later rule's
// account of the value space expansion never produced.
TEST(GeneratorAcceptanceTest, AnExpansionFailureIsTheAnswerTheCallerGets) {
  GenerateOptions opts;
  opts.parameters = {Parameter("n", {"5"}, {true, false}), Parameter("plan", {"free", "pro"})};
  opts.boundary_configs["n"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
  opts.strength = 2;

  auto result = Generate(opts);

  EXPECT_EQ(result.error.code, Error::Code::kInvalidInput);
  EXPECT_EQ(result.error.message, "Invalid metadata length for parameter 'n': invalid");
  ASSERT_FALSE(result.warnings.empty());
  EXPECT_EQ(result.warnings[0], result.error.message);
}

// Seed rows address values by index into the value list the caller declared, so
// expansion must move them onto the value space the engine runs on.
TEST(GeneratorAcceptanceTest, SeedRowsAddressTheDeclaredValueSpace) {
  GenerateOptions opts;
  opts.parameters = {Parameter("n", {"5"}), Parameter("os", {"win", "mac"})};
  opts.boundary_configs["n"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
  opts.strength = 2;
  // n is at its only declared value, os is at "mac".
  opts.seeds = {TestCase{{0, 1}}};

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok()) << result.error.message;
  const uint32_t five = coverwise::model::ResolveValueName(result.parameters[0], "5");
  ASSERT_NE(five, coverwise::model::kUnassigned);
  ASSERT_FALSE(result.tests.empty());
  EXPECT_EQ(result.tests[0].values[0], five);
  EXPECT_EQ(result.tests[0].values[1], 1u);
  // The seed took part in coverage rather than being reported as unusable.
  EXPECT_TRUE(result.warnings.empty()) << result.warnings[0];
}

// Whether a value is a number is decided by the shared decimal grammar. The
// pure-TS port runs the same case, so the two remaps agree index for index.
TEST(GeneratorAcceptanceTest, ValueIdentityFollowsTheSharedNumericGrammar) {
  const std::vector<std::string> spellings = {" 5", "0x10", "5.0", "five"};
  GenerateOptions opts;
  opts.parameters = {Parameter("n", spellings), Parameter("os", {"win", "mac"})};
  opts.boundary_configs["n"] = BoundaryConfig{BoundaryConfig::Type::kInteger, 4.0, 6.0, 1.0};
  opts.strength = 2;
  for (uint32_t index = 0; index < spellings.size(); ++index) {
    opts.seeds.push_back(TestCase{{index, 0}});
  }

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok()) << result.error.message;
  const auto& expanded = result.parameters[0];
  // "5.0" is the only number here, so it absorbs the generated 5 and keeps its
  // own spelling; " 5" and "0x10" are text under the shared grammar and are
  // carried over untouched rather than folded onto a generated value.
  EXPECT_EQ(expanded.values,
            (std::vector<std::string>{"3", "4", "5.0", "6", "7", " 5", "0x10", "five"}));
  ASSERT_GE(result.tests.size(), spellings.size());
  for (size_t index = 0; index < spellings.size(); ++index) {
    EXPECT_EQ(expanded.values[result.tests[index].values[0]], spellings[index]);
  }
}

// An extend mode the engine does not implement is answered with an error. The
// alternative -- treating it as strict, or returning an ok result the caller's
// rows never reached -- loses the suite the caller already had.
TEST(GeneratorAcceptanceTest, ExtendRejectsAModeItDoesNotImplement) {
  GenerateOptions opts;
  opts.parameters = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  opts.strength = 2;
  const std::vector<TestCase> existing = {TestCase{{0, 0}}, TestCase{{1, 1}}};

  auto result = Extend(existing, opts, static_cast<ExtendMode>(99));

  EXPECT_FALSE(result.error.ok());
  EXPECT_EQ(result.error.code, Error::Code::kInvalidInput);
  // Either every row survives or the call fails; an ok result holding fewer
  // rows than it was handed is what must be impossible.
  EXPECT_TRUE(!result.error.ok() || result.tests.size() >= existing.size());

  auto strict = Extend(existing, opts, ExtendMode::kStrict);
  ASSERT_TRUE(strict.error.ok()) << strict.error.message;
  EXPECT_GE(strict.tests.size(), existing.size());
}

// The uncovered report is a diagnostic, so its size is bounded by the
// diagnostic budget while the count it summarizes stays exact. A model that
// stops far short of coverage must not be answered by materializing one
// readable tuple per missing one.
TEST(GeneratorAcceptanceTest, TheUncoveredReportStaysWithinTheDiagnosticBudget) {
  constexpr uint32_t kParams = 30;
  constexpr uint32_t kSubModelParams = 10;
  GenerateOptions opts;
  SubModel sub;
  sub.strength = 2;
  for (uint32_t pi = 0; pi < kParams; ++pi) {
    const std::string name = "p" + std::to_string(pi);
    opts.parameters.push_back({name, {"0", "1"}, {}});
    if (pi < kSubModelParams) sub.parameter_names.push_back(name);
  }
  opts.strength = 2;
  opts.max_tests = 1;
  opts.sub_models = {sub};

  auto result = Generate(opts);

  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_EQ(result.tests.size(), 1u);

  // One test covers one value pair per parameter pair, so the global engine is
  // short by three of the four value pairs of every parameter pair. The
  // sub-model's parameters are a subset of the global model's, so every tuple
  // it is short of is one the global engine is short of too, and the union adds
  // nothing.
  const uint32_t param_pairs = kParams * (kParams - 1) / 2;
  EXPECT_EQ(result.uncovered_count, param_pairs * 3);
  EXPECT_LE(result.uncovered.size(), static_cast<size_t>(coverwise::model::kMaxDiagnosticTuples));
  EXPECT_LT(result.uncovered.size(), result.uncovered_count);
  EXPECT_EQ(result.omitted_uncovered, result.uncovered_count - result.uncovered.size());
}
