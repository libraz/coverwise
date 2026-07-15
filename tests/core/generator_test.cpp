#include "core/generator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
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
  EXPECT_NE(std::find(result.warnings.begin(), result.warnings.end(),
                      "Negative coverage incomplete for A=bad"),
            result.warnings.end());
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
