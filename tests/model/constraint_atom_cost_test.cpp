/// @file constraint_atom_cost_test.cpp
/// @brief Constraint atoms do their string work once, at construction.
///
/// The assertions here are ratios between two runs of the same shape, never
/// absolute durations: only the ratio expresses the invariant, and only the
/// ratio survives being run on a loaded machine or under a sanitizer.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "core/generator.h"
#include "model/constraint_ast.h"
#include "model/generate_options.h"

using coverwise::core::Generate;
using coverwise::model::ConstraintResult;
using coverwise::model::GenerateOptions;
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

  for (uint32_t i = 0; i < values.size(); ++i) {
    std::string folded_value = values[i];
    for (char& c : folded_value) {
      if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    }
    const std::vector<uint32_t> assignment = {i};
    EXPECT_EQ(insensitive.Evaluate(assignment), LikeNode::GlobMatch(pattern, folded_value)
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
  auto [short_ms, long_ms] = FastestMsEach(
      3, [] { Generate(ModelWithLongValues(2, true)); },
      [] { Generate(ModelWithLongValues(512, true)); });

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
