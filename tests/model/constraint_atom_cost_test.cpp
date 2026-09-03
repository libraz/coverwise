/// @file constraint_atom_cost_test.cpp
/// @brief Constraint atoms do their string work once, at construction, and the
///   answers they give are unchanged by having done it early.
///
/// The cost assertions here are ratios between two runs of the same shape,
/// never absolute durations: only the ratio expresses the invariant, and only
/// the ratio survives being run on a loaded machine or under a sanitizer.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/generator.h"
#include "model/constraint_ast.h"
#include "model/generate_options.h"
#include "model/limits.h"
#include "util/string_util.h"
#include "validator/coverage_validator.h"

using coverwise::core::Generate;
using coverwise::model::ConstraintResult;
using coverwise::model::GenerateOptions;
using coverwise::model::InNode;
using coverwise::model::kUnassigned;
using coverwise::model::LikeNode;
using coverwise::model::ParamEqualsNode;
using coverwise::model::ParamNotEqualsNode;

namespace {

using Clock = std::chrono::steady_clock;

/// @brief Fastest run of each of two workloads, sampling them alternately.
///
/// The fastest run is the one least disturbed by scheduling noise, which is the
/// only way a duration ratio means anything on a shared machine.
///
/// Timing one workload to completion and only then the other lets a shift in
/// machine load land wholly on whichever went second, which reads back as a
/// ratio neither workload earned. Alternating puts both through the same load
/// window, and swapping which one leads on alternate rounds keeps the cost of
/// the round itself from being charged to the same one every time. Taking each
/// one's own minimum then keeps the quietest of the windows.
template <typename First, typename Second>
std::pair<double, double> FastestMsEach(int repetitions, First first, Second second) {
  const auto timed = [](auto& work) {
    const auto start = Clock::now();
    work();
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  };
  double first_best = 0.0;
  double second_best = 0.0;
  for (int i = 0; i < repetitions; ++i) {
    const bool first_leads = (i % 2) == 0;
    double first_ms = first_leads ? timed(first) : 0.0;
    const double second_ms = timed(second);
    if (!first_leads) first_ms = timed(first);
    if (i == 0 || first_ms < first_best) first_best = first_ms;
    if (i == 0 || second_ms < second_best) second_best = second_ms;
  }
  return {first_best, second_best};
}

std::vector<std::string> MakeValues(size_t count, size_t length) {
  std::vector<std::string> values;
  values.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    values.push_back(std::string(length, 'v') + std::to_string(i));
  }
  return values;
}

/// @brief Model whose values are @p value_length characters long.
///
/// With @p compare_parameters the constraints are parameter-to-parameter, the
/// shape that used to compare value strings on every evaluation.
///
/// The size is chosen so a run lands in the hundreds of milliseconds. A model
/// small enough to finish in tens of milliseconds measures contention as much as
/// it measures the engine.
GenerateOptions ModelWithLongValues(size_t value_length, bool compare_parameters) {
  constexpr int kParameters = 20;
  constexpr size_t kValuesPerParameter = 8;
  GenerateOptions opts;
  for (int p = 0; p < kParameters; ++p) {
    opts.parameters.push_back(
        {"p" + std::to_string(p), MakeValues(kValuesPerParameter, value_length), {}});
  }
  opts.strength = 2;
  opts.seed = 42;
  if (compare_parameters) {
    for (int i = 0; i + 1 < kParameters; i += 2) {
      opts.constraint_expressions.push_back("p" + std::to_string(i) + " != p" +
                                            std::to_string(i + 1));
    }
  }
  return opts;
}

}  // namespace

TEST(ConstraintAtomCostTest, LikeConstructionDoesNotScaleWithPatternLength) {
  // Both patterns fail on the first codepoint of every value, so matching costs
  // the same for either one and the only pattern-length-dependent work left is
  // decomposing the pattern.
  //
  // The value count is what puts the run in the hundreds of milliseconds, and it
  // also sharpens the comparison: the more values a node holds, the smaller a
  // share of its construction one pattern decomposition can be.
  constexpr size_t kValues = 100'000;
  const auto values = MakeValues(kValues, 8);
  const std::string short_pattern = "z*";
  const std::string long_pattern = std::string(4000, 'z') + "*";

  auto [short_ms, long_ms] = FastestMsEach(
      5, [&] { LikeNode(0, short_pattern, values); }, [&] { LikeNode(0, long_pattern, values); });

  // Decomposing once per node keeps the ratio at 1.0 however long the pattern
  // is. The bound separates that from decomposing once per value, which costs
  // 112x at these sizes — the two regimes are two orders of magnitude apart, so
  // the bound has room to sit far above anything a loaded parallel run produces
  // and still leave the regression no way under it. It is not a budget for
  // construction, and no pattern length moves it.
  EXPECT_LT(long_ms, short_ms * 5.0)
      << "short pattern: " << short_ms << " ms, long pattern: " << long_ms << " ms";
}

TEST(ConstraintAtomCostTest, PrecomputedLikeMatchesAgreeWithTheGlobMatcher) {
  const std::vector<std::string> values = {"win10", "Win11", "mac", "linux", "WINDOWS", ""};
  const std::string pattern = "win*";

  const LikeNode insensitive(0, pattern, values, false);
  const LikeNode sensitive(0, pattern, values, true);
  const std::string folded_pattern = coverwise::util::FoldAsciiString(pattern);

  for (uint32_t i = 0; i < values.size(); ++i) {
    const std::string folded_value = coverwise::util::FoldAsciiString(values[i]);
    const std::vector<uint32_t> assignment = {i};
    EXPECT_EQ(insensitive.Evaluate(assignment), LikeNode::GlobMatch(folded_pattern, folded_value)
                                                    ? ConstraintResult::kTrue
                                                    : ConstraintResult::kFalse)
        << values[i];
    EXPECT_EQ(sensitive.Evaluate(assignment), LikeNode::GlobMatch(pattern, values[i])
                                                  ? ConstraintResult::kTrue
                                                  : ConstraintResult::kFalse)
        << values[i];
  }
}

TEST(ConstraintAtomCostTest, ParameterComparisonEvaluationDoesNotScaleWithValueLength) {
  // Interning the value strings at construction is what makes these two runs
  // cost the same; comparing the strings themselves made the long-value run
  // scale with the length of the values.
  constexpr int kEvaluations = 4'000'000;
  const auto short_left = MakeValues(64, 2);
  const auto short_right = MakeValues(64, 2);
  const auto long_left = MakeValues(64, 512);
  const auto long_right = MakeValues(64, 512);
  const ParamEqualsNode short_node(0, 1, short_left, short_right);
  const ParamEqualsNode long_node(0, 1, long_left, long_right);

  const auto evaluate = [](const ParamEqualsNode& node) {
    return [&node] {
      std::vector<uint32_t> assignment = {0, 0};
      for (int i = 0; i < kEvaluations; ++i) {
        assignment[0] = static_cast<uint32_t>(i % 64);
        assignment[1] = static_cast<uint32_t>((i * 7) % 64);
        (void)node.Evaluate(assignment);
      }
    };
  };

  auto [short_ms, long_ms] = FastestMsEach(3, evaluate(short_node), evaluate(long_node));

  // Interned keys make this a comparison of two uint32 indices, so the honest
  // ratio is 1.0 whatever the values are; anything above it is contention. The
  // bound separates that from the un-interned regime, where each evaluation
  // case-folds and compares both strings — 53x at these value lengths. It is not
  // a budget for the comparison, and no value length can move it: sitting an
  // order of magnitude below the regression, it can afford to sit far enough
  // above 1.0 that a loaded parallel run cannot reach it.
  EXPECT_LT(long_ms, short_ms * 5.0)
      << "short values: " << short_ms << " ms, long values: " << long_ms << " ms";
}

TEST(ConstraintAtomCostTest, GenerationWithParameterComparisonsDoesNotScaleWithValueLength) {
  // The end-to-end form of the same invariant: a model whose constraints
  // compare one parameter against another must not get slower just because its
  // values are longer strings.
  //
  // Both models are built before the timing starts, so what is timed is the
  // generate run and not the cost of assembling the options it reads -- and
  // building the long-valued one costs more than the short, which is exactly
  // the difference this test is trying to attribute elsewhere.
  const GenerateOptions short_model = ModelWithLongValues(2, true);
  const GenerateOptions long_model = ModelWithLongValues(512, true);

  auto [short_ms, long_ms] =
      FastestMsEach(3, [&] { Generate(short_model); }, [&] { Generate(long_model); });

  // Same two regimes as the atom-level gate, seen through a whole generate run,
  // and the bound is again a separator rather than a budget. End-to-end dilutes
  // the signal, but only a little: on this model the constraints account for the
  // bulk of generation, so even a small fraction of that being evaluation leaves
  // the un-interned regime an order of magnitude above the bound. Below it, the
  // ratio is 1.0 by construction and the distance is headroom against the
  // contention of a loaded parallel run.
  EXPECT_LT(long_ms, short_ms * 5.0)
      << "short values: " << short_ms << " ms, long values: " << long_ms << " ms";
}

TEST(ConstraintAtomCostTest, InternedKeysKeepTheCaseFoldingPolicy) {
  const std::vector<std::string> left = {"Alpha", "beta"};
  const std::vector<std::string> right = {"alpha", "GAMMA"};

  const ParamEqualsNode insensitive(0, 1, left, right, false);
  const ParamEqualsNode sensitive(0, 1, left, right, true);
  const ParamNotEqualsNode insensitive_not(0, 1, left, right, false);

  EXPECT_EQ(insensitive.Evaluate({0, 0}), ConstraintResult::kTrue);
  EXPECT_EQ(sensitive.Evaluate({0, 0}), ConstraintResult::kFalse);
  EXPECT_EQ(insensitive_not.Evaluate({0, 0}), ConstraintResult::kFalse);
  EXPECT_EQ(insensitive.Evaluate({1, 1}), ConstraintResult::kFalse);
  EXPECT_EQ(insensitive_not.Evaluate({1, 1}), ConstraintResult::kTrue);
}

TEST(ConstraintAtomCostTest, PrecomputedMembershipKeepsEveryInBranch) {
  const InNode node(0, {1, 3});

  EXPECT_EQ(node.Evaluate({}), ConstraintResult::kUnknown);
  EXPECT_EQ(node.Evaluate({kUnassigned}), ConstraintResult::kUnknown);
  EXPECT_EQ(node.Evaluate({1}), ConstraintResult::kTrue);
  EXPECT_EQ(node.Evaluate({3}), ConstraintResult::kTrue);
  EXPECT_EQ(node.Evaluate({0}), ConstraintResult::kFalse);
  EXPECT_EQ(node.Evaluate({2}), ConstraintResult::kFalse);
  // Past the largest member, and past anything the table holds.
  EXPECT_EQ(node.Evaluate({4}), ConstraintResult::kFalse);
  EXPECT_EQ(node.Evaluate({9999}), ConstraintResult::kFalse);

  // A membership table is indexed by value index, so what bounds the table is
  // the largest index a parameter may have. An index past that belongs to no
  // parameter and is not a member of anything, so it neither matches nor sizes
  // the table -- a set holding one costs what its in-domain members cost.
  const InNode out_of_domain(0, {1, coverwise::model::kMaxValuesPerParameter, UINT32_MAX - 1});
  EXPECT_EQ(out_of_domain.Evaluate({1}), ConstraintResult::kTrue);
  EXPECT_EQ(out_of_domain.Evaluate({coverwise::model::kMaxValuesPerParameter}),
            ConstraintResult::kFalse);
  EXPECT_EQ(out_of_domain.Evaluate({UINT32_MAX - 1}), ConstraintResult::kFalse);

  // An empty set matches nothing but still answers the unassigned branch.
  const InNode empty(0, {});
  EXPECT_EQ(empty.Evaluate({kUnassigned}), ConstraintResult::kUnknown);
  EXPECT_EQ(empty.Evaluate({0}), ConstraintResult::kFalse);

  // A repeated member is one member.
  const InNode repeated(0, {2, 2, 2});
  EXPECT_EQ(repeated.Evaluate({2}), ConstraintResult::kTrue);
  EXPECT_EQ(repeated.Evaluate({1}), ConstraintResult::kFalse);

  // A walk of the member list that dropped the domain bound would answer the
  // out-of-domain cases above differently, so those branches catch it. What no
  // assertion here reaches is the cost of the lookup: that it is one indexed
  // read whatever the set holds. A walk that keeps the bound computes the same
  // predicate the slow way, so it answers every case here exactly as the table
  // does. Evaluation allocates in neither regime, and the node keeps nothing of
  // the set for a test to watch, so only elapsed time separates them -- and a
  // wall-clock ratio measured under a loaded parallel run reports on the
  // machine rather than on the node. The pure-TS port pins it structurally
  // instead, where the set stays reachable from the node and a test can count
  // how often evaluation reads it.
}

TEST(ConstraintAtomCostTest, InConstraintGenerationStaysDeterministicAndOrderIndependent) {
  // Membership is precomputed from the set, so how the set was written must not
  // reach the suite: the same seed produces the same rows, and permuting the
  // set produces the same rows too.
  const auto model = [](const char* set_expression) {
    GenerateOptions opts;
    opts.parameters.push_back({"env", {"dev", "stg", "prod", "qa", "demo"}, {}});
    opts.parameters.push_back({"region", {"us", "eu", "ap"}, {}});
    opts.parameters.push_back({"tier", {"free", "pro", "team"}, {}});
    opts.strength = 2;
    opts.seed = 42;
    opts.constraint_expressions.push_back(std::string("env IN ") + set_expression);
    return opts;
  };
  const auto rows = [](const coverwise::model::GenerateResult& result) {
    std::vector<std::vector<uint32_t>> values;
    values.reserve(result.tests.size());
    for (const auto& test : result.tests) values.push_back(test.values);
    return values;
  };

  const auto first = Generate(model("{dev, stg, prod}"));
  const auto again = Generate(model("{dev, stg, prod}"));
  const auto permuted = Generate(model("{prod, dev, stg}"));

  ASSERT_FALSE(first.tests.empty());
  EXPECT_EQ(rows(first), rows(again));
  EXPECT_EQ(rows(first), rows(permuted));

  // And the constraint holds: no row outside the set reaches the suite.
  for (const auto& test : first.tests) {
    EXPECT_LT(test.values[0], 3u) << "env index " << test.values[0];
  }
}

// Case folding decides which value a name resolves to, so it sits underneath
// every generated row. These are the models whose tuple universe is known
// independently of the generator: the suite must cover that universe, repeat
// exactly for a given seed, and render every value in the case it was defined
// with. Nothing here pins a test count -- the count is the generator's to
// choose, the coverage is not.
TEST(ConstraintAtomCostTest, KnownModelsCoverTheirTupleUniverseAndRepeatExactly) {
  const auto render = [](const coverwise::model::GenerateResult& result) {
    std::vector<std::string> rows;
    rows.reserve(result.tests.size());
    for (const auto& test : result.tests) {
      std::string row;
      for (size_t p = 0; p < test.values.size(); ++p) {
        if (p > 0) row += ',';
        row += result.parameters[p].name;
        row += '=';
        row += result.parameters[p].values[test.values[p]];
      }
      rows.push_back(row);
    }
    return rows;
  };

  // Pair count enumerated from the model shape, not copied from a past run.
  const auto pair_universe = [](const std::vector<coverwise::model::Parameter>& params) {
    uint64_t pairs = 0;
    for (size_t i = 0; i < params.size(); ++i) {
      for (size_t j = i + 1; j < params.size(); ++j) {
        pairs += static_cast<uint64_t>(params[i].values.size()) * params[j].values.size();
      }
    }
    return pairs;
  };

  std::vector<GenerateOptions> models(3);
  models[0].parameters = {{"A", {"0", "1"}, {}}, {"B", {"0", "1"}, {}}, {"C", {"0", "1"}, {}}};
  models[1].parameters = {{"A", {"1", "2", "3"}, {}}, {"B", {"a", "b"}, {}}, {"C", {"x", "y"}, {}}};
  // The same shape spelled in mixed case, which is what the fold acts on.
  models[2].parameters = {{"OsName", {"MacOS", "Ubuntu"}, {}},
                          {"Browser", {"Chrome", "FireFox"}, {}},
                          {"Arch", {"ARM", "x86"}, {}}};
  for (auto& model : models) {
    model.strength = 2;
    model.seed = 42;
  }

  for (const auto& model : models) {
    const auto result = Generate(model);
    const auto repeated = Generate(model);
    ASSERT_TRUE(result.error.ok()) << result.error.message;

    EXPECT_EQ(render(result), render(repeated)) << model.parameters[0].name;

    const auto report =
        coverwise::validator::ValidateCoverage(result.parameters, result.tests, model.strength);
    EXPECT_EQ(report.total_tuples, pair_universe(model.parameters)) << model.parameters[0].name;
    EXPECT_EQ(report.covered_tuples, report.total_tuples) << model.parameters[0].name;
    EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0) << model.parameters[0].name;

    // Every rendered value is one the model defined, spelled the way it was
    // defined -- the fold never reaches the output.
    for (const auto& test : result.tests) {
      for (size_t p = 0; p < test.values.size(); ++p) {
        const auto& defined = model.parameters[p].values;
        const auto& rendered = result.parameters[p].values[test.values[p]];
        EXPECT_NE(std::find(defined.begin(), defined.end(), rendered), defined.end()) << rendered;
      }
    }
  }
}

TEST(ConstraintAtomCostTest, InternedKeysKeepTheUnassignedAndOutOfRangeBranches) {
  const std::vector<std::string> left = {"a", "b"};
  const std::vector<std::string> right = {"a"};

  const ParamEqualsNode equals(0, 1, left, right);
  const ParamNotEqualsNode not_equals(0, 1, left, right);

  EXPECT_EQ(equals.Evaluate({0}), ConstraintResult::kUnknown);
  EXPECT_EQ(equals.Evaluate({kUnassigned, 0}), ConstraintResult::kUnknown);
  EXPECT_EQ(equals.Evaluate({0, kUnassigned}), ConstraintResult::kUnknown);
  EXPECT_EQ(equals.Evaluate({0, 5}), ConstraintResult::kFalse);
  EXPECT_EQ(not_equals.Evaluate({0, 5}), ConstraintResult::kFalse);
}
