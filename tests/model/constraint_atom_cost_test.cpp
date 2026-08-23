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

/// @brief Run @p work @p repetitions times and return the fastest run, in ms.
///
/// The fastest run is the one least disturbed by scheduling noise, which is the
/// only way a duration ratio means anything on a shared machine.
template <typename Work>
double FastestMs(int repetitions, Work work) {
  double best = 0.0;
  for (int i = 0; i < repetitions; ++i) {
    const auto start = Clock::now();
    work();
    const double elapsed = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    if (i == 0 || elapsed < best) best = elapsed;
  }
  return best;
}

std::vector<std::string> MakeValues(size_t count, size_t length) {
  std::vector<std::string> values;
  values.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    values.push_back(std::string(length, 'v') + std::to_string(i));
  }
  return values;
}

/// @brief A 12-parameter model whose values are @p value_length characters long.
///
/// With @p compare_parameters the constraints are parameter-to-parameter, the
/// shape that used to compare value strings on every evaluation.
GenerateOptions ModelWithLongValues(size_t value_length, bool compare_parameters) {
  GenerateOptions opts;
  for (int p = 0; p < 12; ++p) {
    opts.parameters.push_back({"p" + std::to_string(p), MakeValues(5, value_length), {}});
  }
  opts.strength = 2;
  opts.seed = 42;
  if (compare_parameters) {
    for (int i = 0; i + 1 < 12; i += 2) {
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
  // decomposing the pattern. Doing that once per value instead of once per node
  // made the long pattern two orders of magnitude slower to construct.
  const auto values = MakeValues(2000, 8);
  const std::string short_pattern = "z*";
  const std::string long_pattern = std::string(4000, 'z') + "*";

  const double short_ms = FastestMs(3, [&] { LikeNode(0, short_pattern, values); });
  const double long_ms = FastestMs(3, [&] { LikeNode(0, long_pattern, values); });

  EXPECT_LT(long_ms, short_ms * 4.0 + 5.0)
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
  const auto measure = [](size_t value_length) {
    const auto left = MakeValues(64, value_length);
    const auto right = MakeValues(64, value_length);
    const ParamEqualsNode node(0, 1, left, right);
    std::vector<uint32_t> assignment = {0, 0};
    return FastestMs(3, [&] {
      for (int i = 0; i < 200000; ++i) {
        assignment[0] = static_cast<uint32_t>(i % 64);
        assignment[1] = static_cast<uint32_t>((i * 7) % 64);
        (void)node.Evaluate(assignment);
      }
    });
  };

  const double short_ms = measure(2);
  const double long_ms = measure(512);

  EXPECT_LT(long_ms, short_ms * 4.0 + 5.0)
      << "short values: " << short_ms << " ms, long values: " << long_ms << " ms";
}

TEST(ConstraintAtomCostTest, GenerationWithParameterComparisonsDoesNotScaleWithValueLength) {
  // The end-to-end form of the same invariant: a model whose constraints
  // compare one parameter against another must not get slower just because its
  // values are longer strings.
  const double short_ms = FastestMs(3, [] { Generate(ModelWithLongValues(2, true)); });
  const double long_ms = FastestMs(3, [] { Generate(ModelWithLongValues(512, true)); });

  EXPECT_LT(long_ms, short_ms * 3.0 + 10.0)
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
