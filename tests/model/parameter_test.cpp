#include <gtest/gtest.h>

#include "model/parameter.h"

using coverwise::model::Parameter;

TEST(ParameterTest, SizeReturnsValueCount) {
  Parameter p{"color", {"red", "green", "blue"}};
  EXPECT_EQ(p.size(), 3u);
}

TEST(ParameterTest, EmptyParameter) {
  Parameter p{"empty", {}};
  EXPECT_EQ(p.size(), 0u);
}

TEST(ParameterTest, SingleValue) {
  Parameter p{"flag", {"on"}};
  EXPECT_EQ(p.size(), 1u);
}
