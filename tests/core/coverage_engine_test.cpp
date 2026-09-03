#include "core/coverage_engine.h"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "support/allocation_counter.h"

using coverwise::core::CoverageEngine;
using coverwise::model::Parameter;
using coverwise::model::TestCase;
using coverwise::test_support::AllocationCounter;

namespace {

/// @brief Render a complete test case as "name=value,name=value,...".
std::string RenderTestCase(const std::vector<Parameter>& params, const TestCase& test_case) {
  std::string rendered;
  for (size_t pi = 0; pi < params.size(); ++pi) {
    if (pi > 0) rendered += ",";
    rendered += params[pi].name + "=" + params[pi].values[test_case.values[pi]];
  }
  return rendered;
}

/// @brief Drive the deterministic completion loop the generator uses.
///
/// Repeatedly covers the first uncovered tuple, filling the positions the tuple
/// leaves unassigned with each parameter's first value, and returns the test
/// cases in the order they were produced.
/// @param calls Out-parameter receiving the number of FirstUncovered() calls.
std::vector<std::string> DriveCompletion(CoverageEngine& engine,
                                         const std::vector<Parameter>& params, uint32_t& calls) {
  std::vector<std::string> produced;
  calls = 0;
  while (!engine.IsComplete()) {
    CoverageEngine::UncoveredAssignment uncovered;
    ++calls;
    if (!engine.FirstUncovered(uncovered)) break;
    TestCase witness{uncovered.assignment};
    for (size_t pi = 0; pi < params.size(); ++pi) {
      if (witness.values[pi] == coverwise::model::kUnassigned) witness.values[pi] = 0;
    }
    engine.AddTestCase(witness);
    produced.push_back(RenderTestCase(params, witness));
  }
  return produced;
}

}  // namespace

TEST(CoverageEngineTest, EmptyParametersFullCoverage) {
  std::vector<Parameter> params;
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());
  // No parameters = trivially complete
  EXPECT_TRUE(engine.IsComplete());
  EXPECT_DOUBLE_EQ(engine.CoverageRatio(), 1.0);
}

TEST(CoverageEngineTest, TwoParamsPairwise) {
  // 2 params x 2 values. C(2,2) = 1 combination, 2*2 = 4 tuples.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  EXPECT_EQ(engine.TotalTuples(), 4u);
  EXPECT_EQ(engine.CoveredCount(), 0u);
  EXPECT_FALSE(engine.IsComplete());

  // Add all 4 combinations one by one.
  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 1u);

  engine.AddTestCase(TestCase{{0, 1}});
  EXPECT_EQ(engine.CoveredCount(), 2u);

  engine.AddTestCase(TestCase{{1, 0}});
  EXPECT_EQ(engine.CoveredCount(), 3u);
  EXPECT_FALSE(engine.IsComplete());

  engine.AddTestCase(TestCase{{1, 1}});
  EXPECT_EQ(engine.CoveredCount(), 4u);
  EXPECT_TRUE(engine.IsComplete());
  EXPECT_DOUBLE_EQ(engine.CoverageRatio(), 1.0);
}

TEST(CoverageEngineTest, ThreeParamsPairwise) {
  // 3 binary params. C(3,2) = 3 combinations, each 2*2 = 4 tuples. Total = 12.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  EXPECT_EQ(engine.TotalTuples(), 12u);
  EXPECT_EQ(engine.CoveredCount(), 0u);

  // (0,0,0) covers: (A=0,B=0), (A=0,C=0), (B=0,C=0) = 3 new tuples.
  engine.AddTestCase(TestCase{{0, 0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 3u);

  // (1,1,1) covers: (A=1,B=1), (A=1,C=1), (B=1,C=1) = 3 new tuples.
  engine.AddTestCase(TestCase{{1, 1, 1}});
  EXPECT_EQ(engine.CoveredCount(), 6u);

  // (0,1,0) covers: (A=0,B=1), (A=0,C=0)[dup], (B=1,C=0) = 2 new tuples.
  engine.AddTestCase(TestCase{{0, 1, 0}});
  EXPECT_EQ(engine.CoveredCount(), 8u);

  // (1,0,1) covers: (A=1,B=0), (A=1,C=1)[dup], (B=0,C=1) = 2 new tuples.
  engine.AddTestCase(TestCase{{1, 0, 1}});
  EXPECT_EQ(engine.CoveredCount(), 10u);

  // (0,1,1) covers: (A=0,B=1)[dup], (A=0,C=1), (B=1,C=1)[dup] = 1 new tuple.
  engine.AddTestCase(TestCase{{0, 1, 1}});
  EXPECT_EQ(engine.CoveredCount(), 11u);

  // (1,0,0) covers: (A=1,B=0)[dup], (A=1,C=0), (B=0,C=0)[dup] = 1 new tuple.
  engine.AddTestCase(TestCase{{1, 0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 12u);
  EXPECT_TRUE(engine.IsComplete());
}

TEST(CoverageEngineTest, ScoreValueCorrectness) {
  // 2 binary params, pairwise. 4 total tuples.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  constexpr uint32_t kUnassigned = std::numeric_limits<uint32_t>::max();

  // Empty partial: assign A (param 0). B is unassigned, so no pairwise tuple
  // can be completed -> score should be 0.
  TestCase partial;
  partial.values = {kUnassigned, kUnassigned};
  EXPECT_EQ(engine.ScoreValue(partial, 0, 0), 0u);

  // Partial with B=0 assigned. Score of A=0 should be 1 (covers (A=0,B=0)).
  TestCase partial_b0;
  partial_b0.values = {kUnassigned, 0};
  EXPECT_EQ(engine.ScoreValue(partial_b0, 0, 0), 1u);  // (A=0,B=0)
  EXPECT_EQ(engine.ScoreValue(partial_b0, 0, 1), 1u);  // (A=1,B=0)

  // Now cover (A=0,B=0) and re-score.
  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.ScoreValue(partial_b0, 0, 0), 0u);  // already covered
  EXPECT_EQ(engine.ScoreValue(partial_b0, 0, 1), 1u);  // still uncovered
}

TEST(CoverageEngineTest, ScoreCandidateCorrectness) {
  // 3 binary params, pairwise. 12 total tuples.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Score of (0,0,0) on empty coverage: covers 3 new tuples.
  EXPECT_EQ(engine.ScoreCandidate(TestCase{{0, 0, 0}}), 3u);

  // Add (0,0,0), then score (1,1,1): covers 3 more new tuples.
  engine.AddTestCase(TestCase{{0, 0, 0}});
  EXPECT_EQ(engine.ScoreCandidate(TestCase{{1, 1, 1}}), 3u);

  // Score of (0,0,0) again: all its tuples already covered, score = 0.
  EXPECT_EQ(engine.ScoreCandidate(TestCase{{0, 0, 0}}), 0u);

  // Score of (0,1,0): covers (A=0,B=1)[new], (A=0,C=0)[dup], (B=1,C=0)[new] = 2.
  EXPECT_EQ(engine.ScoreCandidate(TestCase{{0, 1, 0}}), 2u);
}

TEST(CoverageEngineTest, TupleExplosionLimit) {
  // Create parameters that would exceed kMaxTuples (16M).
  // 50 params x 10 values, strength 3: C(50,3)=19600, each 10^3=1000,
  // total = 19,600,000 > 16M.
  std::vector<Parameter> params;
  for (int i = 0; i < 50; ++i) {
    Parameter p;
    p.name = "P" + std::to_string(i);
    for (int j = 0; j < 10; ++j) {
      p.values.push_back(std::to_string(j));
    }
    params.push_back(std::move(p));
  }

  auto [engine, err] = CoverageEngine::Create(params, 3);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ(err.code, coverwise::model::Error::Code::kTupleExplosion);
}

TEST(CoverageEngineTest, CombinationMetadataLimitCheckedBeforeMaterialization) {
  std::vector<Parameter> params;
  for (uint32_t pi = 0; pi < 200; ++pi) {
    params.push_back({"P" + std::to_string(pi), {"only"}, {}});
  }
  auto [engine, err] = CoverageEngine::Create(params, 3);
  EXPECT_EQ(err.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_EQ(err.message, "parameter combination metadata exceeds safety limit");
}

TEST(CoverageEngineTest, TupleExplosionErrorMessageParity) {
  // Over-the-limit model must produce a structured kTupleExplosion error whose
  // message is identical to the TypeScript surface (see coverage-engine.test.ts:
  // "tuple explosion error message matches C++"). Pinning both code and message
  // keeps the explosion contract uniform across surfaces.
  std::vector<Parameter> params;
  for (int i = 0; i < 10; ++i) {
    Parameter p;
    p.name = "P" + std::to_string(i);
    for (int j = 0; j < 100; ++j) {
      p.values.push_back(std::to_string(j));
    }
    params.push_back(std::move(p));
  }

  auto [engine, err] = CoverageEngine::Create(params, 5);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ(err.code, coverwise::model::Error::Code::kTupleExplosion);
  EXPECT_EQ(err.message, "t-wise tuple count exceeds safety limit");
  // The detail reports the real (approximate) magnitude — a single combination's
  // product is 100^5 = 10,000,000,000 — not a fixed sentinel just past the limit.
  EXPECT_NE(err.detail.find("10000000000"), std::string::npos) << err.detail;
  EXPECT_EQ(err.detail.find("16000001"), std::string::npos) << err.detail;
}

TEST(CoverageEngineTest, GetUncoveredTuplesContents) {
  // 2 binary params, verify uncovered tuples contain readable strings.
  std::vector<Parameter> params = {
      {"OS", {"win", "mac"}, {}},
      {"Browser", {"chrome", "firefox"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  // Cover 2 out of 4 tuples.
  engine.AddTestCase(TestCase{{0, 0}});  // OS=win, Browser=chrome
  engine.AddTestCase(TestCase{{1, 1}});  // OS=mac, Browser=firefox

  auto uncovered = engine.GetUncoveredTuples(params);
  EXPECT_EQ(uncovered.size(), 2u);

  // Verify the uncovered tuples are the two we didn't add.
  // Should be (OS=win, Browser=firefox) and (OS=mac, Browser=chrome).
  bool found_win_firefox = false;
  bool found_mac_chrome = false;
  for (const auto& ut : uncovered) {
    ASSERT_EQ(ut.tuple.size(), 2u);
    std::string combined = ut.tuple[0] + ", " + ut.tuple[1];
    if (combined == "OS=win, Browser=firefox") found_win_firefox = true;
    if (combined == "OS=mac, Browser=chrome") found_mac_chrome = true;
  }
  EXPECT_TRUE(found_win_firefox);
  EXPECT_TRUE(found_mac_chrome);
}

TEST(CoverageEngineTest, DuplicateTestCaseNoop) {
  // Adding the same test case twice should not change coverage count.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 1u);

  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 1u);  // No change
}

TEST(CoverageEngineTest, TupleExplosionOverflowUint32) {
  // 10 parameters x 100 values each at strength 5.
  // A single combination's product = 100^5 = 10^10 > 4.3 billion (UINT32_MAX).
  // ComputeTotalTuples must detect overflow and return an error.
  std::vector<Parameter> params;
  for (int i = 0; i < 10; ++i) {
    Parameter p;
    p.name = "P" + std::to_string(i);
    for (int j = 0; j < 100; ++j) {
      p.values.push_back(std::to_string(j));
    }
    params.push_back(std::move(p));
  }

  auto [engine, err] = CoverageEngine::Create(params, 5);
  EXPECT_FALSE(err.ok());
  EXPECT_EQ(err.code, coverwise::model::Error::Code::kTupleExplosion);

  // The returned engine should be in a safe default state (not corrupted).
  // Verify it reports zero tuples and is trivially complete.
  auto [engine2, err2] = CoverageEngine::Create({}, 2);
  ASSERT_TRUE(err2.ok());
  EXPECT_TRUE(engine2.IsComplete());
  EXPECT_EQ(engine2.TotalTuples(), 0u);
}

TEST(CoverageEngineTest, SingleParamStrengthOne) {
  // Edge case: 1 parameter, strength 1. Each value is a 1-tuple.
  std::vector<Parameter> params = {
      {"Color", {"red", "green", "blue"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 1);
  ASSERT_TRUE(err.ok());

  EXPECT_EQ(engine.TotalTuples(), 3u);
  EXPECT_FALSE(engine.IsComplete());

  engine.AddTestCase(TestCase{{0}});
  EXPECT_EQ(engine.CoveredCount(), 1u);

  engine.AddTestCase(TestCase{{1}});
  engine.AddTestCase(TestCase{{2}});
  EXPECT_TRUE(engine.IsComplete());
}

TEST(CoverageEngineTest, CompletionOrderIsStableForModelWithInvalidValues) {
  // Invalid values leave large excluded regions in the coverage bitmap, which is
  // exactly where the incremental scan must not change which tuple is picked.
  std::vector<Parameter> params = {
      {"os", {"win", "mac", "beos"}, {false, false, true}},
      {"browser", {"chrome", "safari"}, {}},
      {"auth", {"oauth", "basic", "none"}, {false, false, true}},
      {"region", {"us", "eu"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());
  engine.ExcludeInvalidValues();

  uint32_t calls = 0;
  auto produced = DriveCompletion(engine, params, calls);

  // Pinned suite: the completion loop must keep emitting these test cases, in
  // this order, for the scan to count as behaviour-preserving.
  const std::vector<std::string> expected = {
      "os=win,browser=chrome,auth=oauth,region=us", "os=win,browser=safari,auth=oauth,region=us",
      "os=mac,browser=chrome,auth=oauth,region=us", "os=mac,browser=safari,auth=oauth,region=us",
      "os=win,browser=chrome,auth=basic,region=us", "os=mac,browser=chrome,auth=basic,region=us",
      "os=win,browser=chrome,auth=oauth,region=eu", "os=mac,browser=chrome,auth=oauth,region=eu",
      "os=win,browser=safari,auth=basic,region=us", "os=win,browser=safari,auth=oauth,region=eu",
      "os=win,browser=chrome,auth=basic,region=eu",
  };
  EXPECT_EQ(produced, expected);
  EXPECT_TRUE(engine.IsComplete());
}

TEST(CoverageEngineTest, FirstUncoveredRewindsAfterResetCoverage) {
  // The scan may only skip a prefix while coverage bits are monotonically set.
  // ResetCoverage() clears them, so the next scan must start over from the very
  // first tuple.
  std::vector<Parameter> params = {
      {"A", {"0", "1"}, {}},
      {"B", {"0", "1"}, {}},
      {"C", {"0", "1"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  CoverageEngine::UncoveredAssignment first;
  ASSERT_TRUE(engine.FirstUncovered(first));

  uint32_t calls = 0;
  DriveCompletion(engine, params, calls);
  ASSERT_TRUE(engine.IsComplete());

  engine.ResetCoverage();
  CoverageEngine::UncoveredAssignment after_reset;
  ASSERT_TRUE(engine.FirstUncovered(after_reset));
  EXPECT_EQ(after_reset.index, first.index);
  EXPECT_EQ(after_reset.assignment, first.assignment);
}

TEST(CoverageEngineTest, FirstUncoveredScanCostStaysLinearOverACompletionPass) {
  // 8 params x 4 values at strength 3: C(8,3) = 56 combinations x 64 value
  // tuples = 3584 tuple indices. The last value of every parameter is invalid,
  // so exclusion sets most of those bits up front and the completion loop scans
  // across long excluded stretches -- the case that made a from-scratch rescan
  // quadratic.
  constexpr uint32_t kParamCount = 8;
  constexpr uint64_t kUniverse = 56 * 64;
  std::vector<Parameter> params;
  for (uint32_t pi = 0; pi < kParamCount; ++pi) {
    params.push_back(
        {"P" + std::to_string(pi), {"v0", "v1", "v2", "bad"}, {false, false, false, true}});
  }
  auto [engine, err] = CoverageEngine::Create(params, 3);
  ASSERT_TRUE(err.ok());
  ASSERT_EQ(engine.TotalTuples(), kUniverse);
  engine.ExcludeInvalidValues();

  uint32_t calls = 0;
  DriveCompletion(engine, params, calls);
  ASSERT_TRUE(engine.IsComplete());
  ASSERT_GT(calls, 10u);

  // Every tuple index is examined at most once across the whole pass, plus the
  // one uncovered index each call stops on.
  EXPECT_LE(engine.ScanBitTests(), kUniverse + calls);
  // Restarting each scan at index 0 would test on the order of half the universe
  // per call; this keeps that regression from passing unnoticed.
  EXPECT_LT(engine.ScanBitTests(), calls * kUniverse / 4);
}

TEST(CoverageEngineTest, ConstraintClassificationScalesAcrossManyParameters) {
  constexpr uint32_t kParameterCount = 1500;
  std::vector<Parameter> params;
  params.reserve(kParameterCount);
  for (uint32_t index = 0; index < kParameterCount; ++index) {
    params.push_back({"P" + std::to_string(index), {"v"}, {}});
  }
  auto [engine, err] = CoverageEngine::Create(params, 1);
  ASSERT_TRUE(err.ok()) << err.message;
  std::vector<coverwise::model::Constraint> constraints;
  constraints.push_back(std::make_unique<coverwise::model::EqualsNode>(0, 0));
  bool budget_exceeded = false;

  engine.ExcludeInvalidTuples(constraints, {}, &budget_exceeded);

  EXPECT_FALSE(budget_exceeded);
  EXPECT_EQ(engine.TotalTuples(), kParameterCount);
}

TEST(CoverageEngineTest, ScoringAWholeParameterAgreesWithScoringEachValue) {
  std::vector<Parameter> params = {
      {"A", {"a0", "a1", "a2"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1", "c2", "c3"}, {}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());
  engine.AddTestCase(TestCase{{0, 0, 0}});
  engine.AddTestCase(TestCase{{1, 1, 2}});

  // Partial assignments covering the interesting shapes: nothing else assigned,
  // one neighbour assigned, and every neighbour assigned.
  const std::vector<TestCase> partials = {
      TestCase{{coverwise::model::kUnassigned, coverwise::model::kUnassigned,
                coverwise::model::kUnassigned}},
      TestCase{{coverwise::model::kUnassigned, 1, coverwise::model::kUnassigned}},
      TestCase{{coverwise::model::kUnassigned, 0, 3}},
  };
  for (const auto& partial : partials) {
    for (uint32_t pi = 0; pi < params.size(); ++pi) {
      if (partial.values[pi] != coverwise::model::kUnassigned) continue;
      std::vector<uint32_t> batched(params[pi].size(), 0);
      engine.AddValueScores(partial, pi, batched.data());
      for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
        EXPECT_EQ(batched[vi], engine.ScoreValue(partial, pi, vi))
            << "param " << pi << " value " << vi;
      }
    }
  }
}

TEST(CoverageEngineTest, ScoringAParameterCostsTheSameAtAnyValueCount) {
  // Scoring one value at a time re-walks the parameter's combinations once per
  // value. The visit counter must stay at one walk per call, so a parameter with
  // ten times the values costs the same number of combination visits.
  auto build = [](uint32_t value_count) {
    std::vector<Parameter> params;
    for (uint32_t pi = 0; pi < 4; ++pi) {
      Parameter p;
      p.name = "P" + std::to_string(pi);
      for (uint32_t vi = 0; vi < value_count; ++vi) p.values.push_back("v" + std::to_string(vi));
      params.push_back(std::move(p));
    }
    return params;
  };

  auto visits_for = [&](uint32_t value_count) {
    auto params = build(value_count);
    auto [engine, err] = CoverageEngine::Create(params, 2);
    EXPECT_TRUE(err.ok());
    TestCase partial{{coverwise::model::kUnassigned, 0, 0, 0}};
    std::vector<uint32_t> scores(value_count, 0);
    engine.AddValueScores(partial, 0, scores.data());
    return engine.ValueScoreComboVisits();
  };

  const uint64_t few = visits_for(2);
  const uint64_t many = visits_for(20);
  EXPECT_GT(few, 0u);
  EXPECT_EQ(few, many);
}

TEST(CoverageEngineTest, ZeroStrengthYieldsAnEmptyTupleUniverse) {
  // Strength 0 selects no parameters, so there is no combination stride to
  // divide by and no tuple to cover. Both factories must agree on that instead
  // of evaluating an undefined expression.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
  };

  auto [engine, err] = CoverageEngine::Create(params, 0);
  ASSERT_TRUE(err.ok()) << err.message;
  EXPECT_EQ(engine.TotalTuples(), 0u);
  EXPECT_EQ(engine.CoveredCount(), 0u);
  EXPECT_DOUBLE_EQ(engine.CoverageRatio(), 1.0);
  EXPECT_TRUE(engine.IsComplete());
  CoverageEngine::UncoveredAssignment uncovered;
  EXPECT_FALSE(engine.FirstUncovered(uncovered));
  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 0u);
  EXPECT_TRUE(engine.GetUncoveredTuples(params).empty());

  std::vector<uint32_t> subset = {0};
  auto [sub_engine, sub_err] = CoverageEngine::Create(params, subset, 0);
  ASSERT_TRUE(sub_err.ok()) << sub_err.message;
  EXPECT_EQ(sub_engine.TotalTuples(), 0u);
  EXPECT_TRUE(sub_engine.IsComplete());
}

TEST(CoverageEngineTest, EnginesOverOneModelShareASingleParameterSet) {
  // A sub-model engine indexes the whole parameter set by global index, so it
  // needs all of them available. Sharing keeps that from duplicating the model's
  // string payload once per engine.
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  auto shared = CoverageEngine::ShareParameters(params);

  auto [global, global_err] = CoverageEngine::CreateShared(shared, 2);
  ASSERT_TRUE(global_err.ok());
  auto [sub_ab, sub_ab_err] = CoverageEngine::CreateShared(shared, {0, 1}, 2);
  ASSERT_TRUE(sub_ab_err.ok());
  auto [sub_bc, sub_bc_err] = CoverageEngine::CreateShared(shared, {1, 2}, 2);
  ASSERT_TRUE(sub_bc_err.ok());

  EXPECT_EQ(&global.Parameters(), &sub_ab.Parameters());
  EXPECT_EQ(&global.Parameters(), &sub_bc.Parameters());
  EXPECT_EQ(global.Parameters().size(), params.size());

  // Sharing must not change what a subset engine enumerates.
  EXPECT_EQ(global.TotalTuples(), 12u);
  EXPECT_EQ(sub_ab.TotalTuples(), 4u);
  auto [copied, copied_err] = CoverageEngine::Create(params, {0, 1}, 2);
  ASSERT_TRUE(copied_err.ok());
  EXPECT_EQ(copied.TotalTuples(), sub_ab.TotalTuples());
}

namespace {

/// @brief What the two ways of reading uncovered tuples cost on one model.
struct UncoveredCost {
  uint64_t uncovered = 0;         ///< Tuples the engine is short of.
  uint64_t walk_allocations = 0;  ///< Allocations made by ForEachUncoveredTuple().
  uint64_t listed = 0;            ///< Tuples GetUncoveredTuples() returned.
  uint64_t list_allocations = 0;  ///< Allocations made by GetUncoveredTuples().
};

/// @brief Read every uncovered tuple of a ten-parameter model both ways.
UncoveredCost MeasureUncoveredCost(uint32_t values_per_param) {
  std::vector<Parameter> params;
  for (uint32_t pi = 0; pi < 10; ++pi) {
    std::vector<std::string> values;
    for (uint32_t vi = 0; vi < values_per_param; ++vi) {
      values.push_back(std::to_string(vi));
    }
    params.push_back({"p" + std::to_string(pi), values, {}});
  }
  auto created = CoverageEngine::Create(params, 2);
  EXPECT_TRUE(created.second.ok());
  const CoverageEngine& engine = created.first;

  UncoveredCost cost;
  cost.uncovered = engine.TotalTuples() - engine.CoveredCount();
  {
    uint64_t visited = 0;
    AllocationCounter counter;
    engine.ForEachUncoveredTuple([&](const uint32_t*, const uint32_t*) {
      ++visited;
      return true;
    });
    cost.walk_allocations = counter.Stop();
    EXPECT_EQ(visited, cost.uncovered);
  }
  {
    AllocationCounter counter;
    auto listed = engine.GetUncoveredTuples(params);
    cost.list_allocations = counter.Stop();
    cost.listed = listed.size();
  }
  return cost;
}

}  // namespace

// Neither way of reading uncovered tuples costs in proportion to how many there
// are: the walk builds nothing per tuple, and the readable list is bounded by
// the diagnostic budget. Both models below are short of far more tuples than
// that budget, and by a factor of four from each other.
TEST(CoverageEngineDiagnosticTest, ReadingUncoveredTuplesCostsTheDiagnosticBudget) {
  const UncoveredCost smaller = MeasureUncoveredCost(8);
  const UncoveredCost larger = MeasureUncoveredCost(16);

  ASSERT_GT(smaller.uncovered, coverwise::model::kMaxDiagnosticTuples);
  ASSERT_GT(larger.uncovered, smaller.uncovered * 3);

  // The walk owns two buffers sized by the strength and nothing else.
  EXPECT_EQ(larger.walk_allocations, smaller.walk_allocations);
  EXPECT_LE(larger.walk_allocations, 4u);

  // The readable list stops at the budget, so its cost is the same on both.
  EXPECT_EQ(smaller.listed, coverwise::model::kMaxDiagnosticTuples);
  EXPECT_EQ(larger.listed, smaller.listed);
  EXPECT_EQ(larger.list_allocations, smaller.list_allocations);
}

// The overlap decision between two engines is answered on plain indices, and it
// answers the same question the tuple's own engine does.
TEST(CoverageEngineDiagnosticTest, NeedsTupleAnswersForATupleAnotherEngineEnumerated) {
  std::vector<Parameter> params = {
      {"A", {"a0", "a1"}, {}},
      {"B", {"b0", "b1"}, {}},
      {"C", {"c0", "c1"}, {}},
  };
  auto shared = CoverageEngine::ShareParameters(params);
  auto global_result = CoverageEngine::CreateShared(shared, 2);
  ASSERT_TRUE(global_result.second.ok());
  auto sub_result = CoverageEngine::CreateShared(shared, {0, 1}, 2);
  ASSERT_TRUE(sub_result.second.ok());
  CoverageEngine& global = global_result.first;
  CoverageEngine& sub_ab = sub_result.first;

  global.AddTestCase(TestCase{{0, 0, 0}});

  sub_ab.ForEachUncoveredTuple([&](const uint32_t* combo, const uint32_t* value_indices) {
    const bool covered_by_the_test = value_indices[0] == 0 && value_indices[1] == 0;
    EXPECT_EQ(global.NeedsTuple(combo, value_indices, 2), !covered_by_the_test);
    // A tuple of a size this engine does not enumerate is never one of its own.
    EXPECT_FALSE(global.NeedsTuple(combo, value_indices, 3));
    return true;
  });

  // A parameter combination outside the subset engine is not its tuple either,
  // even while the global engine still needs it.
  const uint32_t bc_params[] = {1, 2};
  const uint32_t bc_values[] = {1, 1};
  EXPECT_TRUE(global.NeedsTuple(bc_params, bc_values, 2));
  EXPECT_FALSE(sub_ab.NeedsTuple(bc_params, bc_values, 2));
}
