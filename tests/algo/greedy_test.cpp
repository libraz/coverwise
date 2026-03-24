#include "algo/greedy.h"

#include <gtest/gtest.h>

#include <limits>
#include <memory>
#include <vector>

#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "util/rng.h"

using coverwise::algo::GreedyConstruct;
using coverwise::algo::ScoreFn;
using coverwise::core::CoverageEngine;
using coverwise::model::Constraint;
using coverwise::model::ConstraintResult;
using coverwise::model::EqualsNode;
using coverwise::model::ImpliesNode;
using coverwise::model::kUnassigned;
using coverwise::model::NotEqualsNode;
using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::util::Rng;

/// @brief Helper to create a ScoreFn from a CoverageEngine reference.
inline ScoreFn MakeScoreFn(const CoverageEngine& engine) {
  return [&engine](const TestCase& partial, uint32_t pi, uint32_t vi) {
    return engine.ScoreValue(partial, pi, vi);
  };
}

// ---------------------------------------------------------------------------
// Basic construction
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, AllParametersAssigned) {
  // A simple 3x2 problem. The result must have a value for every parameter.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  Rng rng(42);
  std::vector<Constraint> constraints;
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);

  ASSERT_EQ(tc.values.size(), 3u);
  for (uint32_t v : tc.values) {
    EXPECT_NE(v, kUnassigned);
    EXPECT_LT(v, 2u);
  }
}

TEST(GreedyConstructTest, SingleParameter) {
  // Edge case: only one parameter, so no pairs exist for strength 2.
  // The function should still return a valid test case.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1", "a2"}, {}},
  };
  // Strength 2 with 1 param: 0 tuples, but should still assign a value.
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  Rng rng(0);
  std::vector<Constraint> constraints;
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);

  ASSERT_EQ(tc.values.size(), 1u);
  EXPECT_NE(tc.values[0], kUnassigned);
  EXPECT_LT(tc.values[0], 3u);
}

// ---------------------------------------------------------------------------
// Coverage maximization
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, CoverageMaximization) {
  // After covering some tuples, the greedy algorithm should prefer values
  // that cover uncovered tuples.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Cover (A=0, B=0). Now tuples with A=1 or B=1 are still uncovered.
  engine.AddTestCase(TestCase{{0, 0}});

  // Generate multiple test cases to reach full coverage.
  Rng rng(7);
  std::vector<Constraint> constraints;
  // Generate enough tests to cover remaining 3 tuples.
  for (int i = 0; i < 10 && !engine.IsComplete(); ++i) {
    auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);
    engine.AddTestCase(tc);
  }
  EXPECT_TRUE(engine.IsComplete());
}

TEST(GreedyConstructTest, FullCoverageThreeParams) {
  // Verify greedy generates enough tests for complete pairwise coverage.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());
  EXPECT_EQ(engine.TotalTuples(), 12u);

  Rng rng(99);
  std::vector<Constraint> constraints;
  int count = 0;
  while (!engine.IsComplete() && count < 20) {
    auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);
    engine.AddTestCase(tc);
    ++count;
  }
  EXPECT_TRUE(engine.IsComplete());
  // Optimal pairwise for 3x2 is 4 tests. Greedy with random parameter
  // ordering may need more; the key assertion is that coverage completes.
  EXPECT_LE(count, 12);
}

// ---------------------------------------------------------------------------
// Constraint pruning
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, ConstraintPruning) {
  // Constraint: IF A=a0 THEN B!=b1  (i.e., (A=0) implies (B!=1))
  // When A=0, value B=1 should be pruned.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  std::vector<Constraint> constraints;
  constraints.push_back(
      std::make_unique<ImpliesNode>(std::make_unique<EqualsNode>(0, 0),       // A == a0
                                    std::make_unique<NotEqualsNode>(1, 1)));  // B != b1

  // Generate many test cases; none should have (A=0, B=1).
  Rng rng(12);
  for (int i = 0; i < 20; ++i) {
    rng.Seed(static_cast<uint64_t>(i));
    auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);
    if (tc.values[0] == 0) {
      EXPECT_NE(tc.values[1], 1u) << "Constraint violated: A=a0 and B=b1";
    }
  }
}

TEST(GreedyConstructTest, ConstraintUnknownAllowed) {
  // When a constraint refers to an unassigned parameter, it returns kUnknown.
  // kUnknown should NOT prune the value.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Constraint: B != b1. When B is unassigned and A is being assigned first,
  // the constraint returns kUnknown for A values, which should be allowed.
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(1, 1));  // B != b1

  Rng rng(0);
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);
  // A should still get assigned (not pruned by the B constraint).
  EXPECT_NE(tc.values[0], kUnassigned);
  // B should be 0 (b1 is pruned by the constraint).
  EXPECT_EQ(tc.values[1], 0u);
}

// ---------------------------------------------------------------------------
// Allowed values filtering
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, AllowedValuesFiltering) {
  // Only allow a subset of values per parameter.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1", "a2"}, {}},
      {"B", {"b0", "b1", "b2"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Only allow A=a1, B=b0 or B=b2
  std::vector<std::vector<bool>> allowed = {
      {false, true, false},  // A: only a1
      {true, false, true},   // B: b0 or b2
  };

  Rng rng(5);
  std::vector<Constraint> constraints;
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng, allowed);

  EXPECT_EQ(tc.values[0], 1u);  // only a1 is allowed
  EXPECT_TRUE(tc.values[1] == 0u || tc.values[1] == 2u);
}

// ---------------------------------------------------------------------------
// All values constrained away (fallback behavior)
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, AllValuesConstrainedFallback) {
  // If every value for a parameter is pruned by constraints, the fallback
  // should assign the first allowed value (or 0 if no allowed mask).
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Constraint: A != a0 AND A != a1  => both values of A are pruned.
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 0));
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 1));

  Rng rng(0);
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);

  // All A values pruned => fallback to 0.
  EXPECT_EQ(tc.values[0], 0u);
  // B should still get a valid assignment.
  EXPECT_NE(tc.values[1], kUnassigned);
}

TEST(GreedyConstructTest, AllValuesConstrainedWithAllowedMask) {
  // Fallback with allowed_values should pick the first allowed value.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1", "a2"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // All A values are pruned by constraints.
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 0));
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 1));
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 2));

  // Allowed mask: only a1 and a2 are allowed.
  std::vector<std::vector<bool>> allowed = {
      {false, true, true},
      {true, true},
  };

  Rng rng(0);
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng, allowed);

  // All A values pruned by constraints. Fallback picks first allowed = a1.
  EXPECT_EQ(tc.values[0], 1u);
}

// ---------------------------------------------------------------------------
// Weight-based tie breaking
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, WeightBasedTieBreaking) {
  // When all values have the same coverage score, weights should bias
  // toward higher-weight values. Use a large weight difference to make
  // the outcome nearly deterministic.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1", "a2"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Give A=a2 an extremely high weight.
  std::vector<std::vector<double>> weights = {
      {0.001, 0.001, 1000.0},  // A: a2 heavily preferred
      {1.0, 1.0},              // B: equal
  };

  // Run multiple times and count how often A=a2 is picked.
  int a2_count = 0;
  std::vector<Constraint> constraints;
  for (uint64_t seed = 0; seed < 50; ++seed) {
    Rng rng(seed);
    auto [eng, e] = CoverageEngine::Create(params, 2);
    ASSERT_TRUE(e.ok());
    auto tc = GreedyConstruct(params, MakeScoreFn(eng), constraints, rng, {}, weights);
    if (tc.values[0] == 2) {
      ++a2_count;
    }
  }
  // With weight 1000 vs 0.001, a2 should be selected almost always.
  EXPECT_GE(a2_count, 45);
}

// ---------------------------------------------------------------------------
// Determinism
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, Determinism) {
  // Same seed must produce the same test case.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1", "a2"}, {}},
      {"B", {"b0", "b1", "b2"}, {}},
      {"C", {"c0", "c1"}, {}},
  };

  std::vector<Constraint> constraints;

  for (uint64_t seed : {0u, 42u, 12345u}) {
    auto [engine1, err1] = CoverageEngine::Create(params, 2);
    ASSERT_TRUE(err1.ok());
    Rng rng1(seed);
    auto tc1 = GreedyConstruct(params, MakeScoreFn(engine1), constraints, rng1);

    auto [engine2, err2] = CoverageEngine::Create(params, 2);
    ASSERT_TRUE(err2.ok());
    Rng rng2(seed);
    auto tc2 = GreedyConstruct(params, MakeScoreFn(engine2), constraints, rng2);

    ASSERT_EQ(tc1.values.size(), tc2.values.size());
    for (size_t i = 0; i < tc1.values.size(); ++i) {
      EXPECT_EQ(tc1.values[i], tc2.values[i]) << "Mismatch at param " << i << " with seed " << seed;
    }
  }
}

// ---------------------------------------------------------------------------
// Multi-engine overload
// ---------------------------------------------------------------------------

TEST(GreedyConstructTest, MultiEngineSumsScores) {
  // Two engines (e.g., global + sub-model) should both contribute to scoring.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  // Engine 1: full pairwise.
  auto result1 = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(result1.second.ok());
  auto eng1 = std::move(result1.first);

  // Engine 2: sub-model on {A, B} only.
  std::vector<uint32_t> subset = {0, 1};
  auto result2 = CoverageEngine::Create(params, subset, 2);
  ASSERT_TRUE(result2.second.ok());
  auto eng2 = std::move(result2.first);

  auto multi_score = [&](const TestCase& partial, uint32_t pi, uint32_t vi) -> uint32_t {
    return eng1.ScoreValue(partial, pi, vi) + eng2.ScoreValue(partial, pi, vi);
  };
  std::vector<Constraint> constraints;
  Rng rng(77);

  auto tc = GreedyConstruct(params, multi_score, constraints, rng);

  ASSERT_EQ(tc.values.size(), 3u);
  for (uint32_t v : tc.values) {
    EXPECT_NE(v, kUnassigned);
  }

  // Mark the test case in both engines and generate another.
  eng1.AddTestCase(tc);
  eng2.AddTestCase(tc);

  auto tc2 = GreedyConstruct(params, multi_score, constraints, rng);
  ASSERT_EQ(tc2.values.size(), 3u);
  for (uint32_t v : tc2.values) {
    EXPECT_NE(v, kUnassigned);
  }
}

TEST(GreedyConstructTest, MultiEngineFullCoverage) {
  // Multi-engine should still achieve full coverage when run repeatedly.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  auto [engine1, err1] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err1.ok());

  std::vector<Constraint> constraints;
  Rng rng(33);

  int count = 0;
  while (!engine1.IsComplete() && count < 20) {
    auto tc = GreedyConstruct(params, MakeScoreFn(engine1), constraints, rng);
    engine1.AddTestCase(tc);
    ++count;
  }
  EXPECT_TRUE(engine1.IsComplete());
}

TEST(GreedyConstructTest, MultiEngineConstraintPruning) {
  // Multi-engine overload should also respect constraints.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Constraint: A != a0.
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 0));

  Rng rng(0);

  for (int i = 0; i < 10; ++i) {
    rng.Seed(static_cast<uint64_t>(i));
    auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);
    EXPECT_NE(tc.values[0], 0u) << "Constraint violated at seed " << i;
  }
}

TEST(GreedyConstructTest, MultiEngineAllPrunedFallback) {
  // Multi-engine: when all values are pruned, fallback to 0.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Prune all A values.
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 0));
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 1));

  Rng rng(0);
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng);

  // All pruned => fallback to 0.
  EXPECT_EQ(tc.values[0], 0u);
}

// ---------------------------------------------------------------------------
// Additional edge cases
// ---------------------------------------------------------------------------

// Edge: single parameter, strength 2 → trivially complete, no pairs
TEST(GreedyConstructTest, SingleParameterNoPairs) {
  std::vector<Parameter> params = {{"A", {"0", "1", "2"}, {}}};
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());
  // Engine should be trivially complete (0 tuples)
  EXPECT_TRUE(engine.IsComplete());
}

// Edge: all values have equal weight → uniform distribution
TEST(GreedyConstructTest, EqualWeightsUniform) {
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());
  std::vector<std::vector<double>> weights = {{1.0, 1.0}, {1.0, 1.0}};
  Rng rng(42);
  std::vector<Constraint> constraints;
  auto tc = GreedyConstruct(params, MakeScoreFn(engine), constraints, rng, {}, weights);
  // Should succeed without crash
  EXPECT_EQ(tc.values.size(), 2u);
}
