#include <gtest/gtest.h>

#include <limits>

#include "core/coverage_engine.h"
#include "model/parameter.h"

using coverwise::core::CoverageEngine;
using coverwise::model::Parameter;
using coverwise::model::TestCase;

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
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
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
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
      {"C", {"0", "1"}},
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
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
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
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
      {"C", {"0", "1"}},
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

TEST(CoverageEngineTest, GetUncoveredTuplesContents) {
  // 2 binary params, verify uncovered tuples contain readable strings.
  std::vector<Parameter> params = {
      {"OS", {"win", "mac"}},
      {"Browser", {"chrome", "firefox"}},
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
      {"A", {"0", "1"}},
      {"B", {"0", "1"}},
  };
  auto [engine, err] = CoverageEngine::Create(params, 2);
  ASSERT_TRUE(err.ok());

  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 1u);

  engine.AddTestCase(TestCase{{0, 0}});
  EXPECT_EQ(engine.CoveredCount(), 1u);  // No change
}

TEST(CoverageEngineTest, SingleParamStrengthOne) {
  // Edge case: 1 parameter, strength 1. Each value is a 1-tuple.
  std::vector<Parameter> params = {
      {"Color", {"red", "green", "blue"}},
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
