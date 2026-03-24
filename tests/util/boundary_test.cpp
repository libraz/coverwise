#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/parameter.h"
#include "util/boundary.h"
#include "validator/coverage_validator.h"

using coverwise::model::Parameter;
using coverwise::util::BoundaryConfig;
using coverwise::util::ExpandBoundaryValues;

TEST(BoundaryTest, IntegerExpansion) {
  Parameter param("age", {});
  BoundaryConfig config;
  config.type = BoundaryConfig::Type::kInteger;
  config.min_value = 1;
  config.max_value = 100;

  auto result = ExpandBoundaryValues(param, config);

  EXPECT_EQ(result.name, "age");
  // Expected: 0, 1, 2, 99, 100, 101
  ASSERT_EQ(result.values.size(), 6u);
  EXPECT_EQ(result.values[0], "0");
  EXPECT_EQ(result.values[1], "1");
  EXPECT_EQ(result.values[2], "2");
  EXPECT_EQ(result.values[3], "99");
  EXPECT_EQ(result.values[4], "100");
  EXPECT_EQ(result.values[5], "101");
}

TEST(BoundaryTest, IntegerExpansionSmallRange) {
  // Range [0, 2]: boundary values -1, 0, 1, 1, 2, 3 -> dedup: -1, 0, 1, 2, 3
  Parameter param("count", {});
  BoundaryConfig config;
  config.type = BoundaryConfig::Type::kInteger;
  config.min_value = 0;
  config.max_value = 2;

  auto result = ExpandBoundaryValues(param, config);

  EXPECT_EQ(result.name, "count");
  // Expected: -1, 0, 1, 2, 3 (min+1 == max-1 == 1, deduped)
  ASSERT_EQ(result.values.size(), 5u);
  EXPECT_EQ(result.values[0], "-1");
  EXPECT_EQ(result.values[1], "0");
  EXPECT_EQ(result.values[2], "1");
  EXPECT_EQ(result.values[3], "2");
  EXPECT_EQ(result.values[4], "3");
}

TEST(BoundaryTest, FloatExpansion) {
  Parameter param("ratio", {});
  BoundaryConfig config;
  config.type = BoundaryConfig::Type::kFloat;
  config.min_value = 0.0;
  config.max_value = 1.0;
  config.step = 0.1;

  auto result = ExpandBoundaryValues(param, config);

  EXPECT_EQ(result.name, "ratio");
  // Expected: -0.1, 0.0, 0.1, 0.9, 1.0, 1.1
  ASSERT_EQ(result.values.size(), 6u);
  // Verify the values are sorted numerically.
  // Parse all as doubles and verify ordering.
  std::vector<double> nums;
  for (const auto& v : result.values) {
    nums.push_back(std::stod(v));
  }
  EXPECT_NEAR(nums[0], -0.1, 1e-9);
  EXPECT_NEAR(nums[1], 0.0, 1e-9);
  EXPECT_NEAR(nums[2], 0.1, 1e-9);
  EXPECT_NEAR(nums[3], 0.9, 1e-9);
  EXPECT_NEAR(nums[4], 1.0, 1e-9);
  EXPECT_NEAR(nums[5], 1.1, 1e-9);
}

TEST(BoundaryTest, MergeWithExplicitValues) {
  // Explicit values include some boundary values and some extras.
  Parameter param("score", {"0", "50", "100"});
  BoundaryConfig config;
  config.type = BoundaryConfig::Type::kInteger;
  config.min_value = 0;
  config.max_value = 100;

  auto result = ExpandBoundaryValues(param, config);

  EXPECT_EQ(result.name, "score");
  // Boundary: -1, 0, 1, 99, 100, 101
  // Explicit: 0, 50, 100
  // Merged + dedup + sorted: -1, 0, 1, 50, 99, 100, 101
  ASSERT_EQ(result.values.size(), 7u);
  EXPECT_EQ(result.values[0], "-1");
  EXPECT_EQ(result.values[1], "0");
  EXPECT_EQ(result.values[2], "1");
  EXPECT_EQ(result.values[3], "50");
  EXPECT_EQ(result.values[4], "99");
  EXPECT_EQ(result.values[5], "100");
  EXPECT_EQ(result.values[6], "101");
}

TEST(BoundaryTest, MergeWithExplicitNonNumericValues) {
  // Mix of numeric and non-numeric explicit values.
  Parameter param("level", {"low", "50", "high"});
  BoundaryConfig config;
  config.type = BoundaryConfig::Type::kInteger;
  config.min_value = 0;
  config.max_value = 100;

  auto result = ExpandBoundaryValues(param, config);

  // Numeric: 50 + boundary(-1, 0, 1, 99, 100, 101) = -1, 0, 1, 50, 99, 100, 101
  // Non-numeric: "low", "high" appended at end
  ASSERT_EQ(result.values.size(), 9u);
  EXPECT_EQ(result.values[0], "-1");
  EXPECT_EQ(result.values[1], "0");
  EXPECT_EQ(result.values[2], "1");
  EXPECT_EQ(result.values[3], "50");
  EXPECT_EQ(result.values[4], "99");
  EXPECT_EQ(result.values[5], "100");
  EXPECT_EQ(result.values[6], "101");
  EXPECT_EQ(result.values[7], "low");
  EXPECT_EQ(result.values[8], "high");
}

TEST(BoundaryTest, GenerationWithBoundaryValues) {
  // Test that generation with boundary expansion produces 100% coverage.
  coverwise::core::GenerateOptions opts;
  opts.parameters = {
      {"age", {}},
      {"browser", {"chrome", "firefox", "safari"}},
  };
  opts.boundary_configs["age"] = {BoundaryConfig::Type::kInteger, 1, 100, 1.0};
  opts.strength = 2;
  opts.seed = 42;

  auto result = coverwise::core::Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());
  EXPECT_GT(result.tests.size(), 0u);

  // Verify that expanded params were used: age should have 6 boundary values.
  // The tests should reference values 0..5 for the age parameter.
  std::set<uint32_t> age_values_seen;
  for (const auto& tc : result.tests) {
    ASSERT_GE(tc.values.size(), 2u);
    age_values_seen.insert(tc.values[0]);
  }
  // All 6 age boundary values should appear.
  EXPECT_EQ(age_values_seen.size(), 6u);
}

TEST(BoundaryTest, GenerationWithBoundaryAndExplicitValues) {
  // Parameter has explicit values + boundary config -> merged.
  coverwise::core::GenerateOptions opts;
  opts.parameters = {
      {"score", {"50"}},
      {"status", {"pass", "fail"}},
  };
  opts.boundary_configs["score"] = {BoundaryConfig::Type::kInteger, 0, 100, 1.0};
  opts.strength = 2;
  opts.seed = 42;

  auto result = coverwise::core::Generate(opts);

  EXPECT_DOUBLE_EQ(result.coverage, 1.0);
  EXPECT_TRUE(result.uncovered.empty());

  // score expanded: -1, 0, 1, 50, 99, 100, 101 = 7 values
  // Validate coverage with the expanded parameters.
  // We need to manually build the expanded params to validate.
  Parameter expanded_score = ExpandBoundaryValues(opts.parameters[0],
                                                  opts.boundary_configs["score"]);
  std::vector<Parameter> expanded_params = {expanded_score, opts.parameters[1]};
  auto report = coverwise::validator::ValidateCoverage(expanded_params, result.tests, opts.strength);
  EXPECT_DOUBLE_EQ(report.coverage_ratio, 1.0);
}
