#include <gtest/gtest.h>

#include "model/test_case.h"

using coverwise::model::GenerateResult;
using coverwise::model::TestCase;

TEST(TestCaseTest, DefaultConstruction) {
  TestCase tc;
  EXPECT_TRUE(tc.values.empty());
}

TEST(TestCaseTest, WithValues) {
  TestCase tc{{0, 1, 2}};
  EXPECT_EQ(tc.values.size(), 3u);
  EXPECT_EQ(tc.values[0], 0u);
  EXPECT_EQ(tc.values[1], 1u);
  EXPECT_EQ(tc.values[2], 2u);
}

TEST(GenerateResultTest, DefaultCoverage) {
  GenerateResult result;
  EXPECT_TRUE(result.tests.empty());
  EXPECT_DOUBLE_EQ(result.coverage, 0.0);
}
