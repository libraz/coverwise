#include "util/rng.h"

#include <gtest/gtest.h>

using coverwise::util::Rng;

TEST(RngTest, DeterministicWithSameSeed) {
  Rng a(42);
  Rng b(42);

  for (int i = 0; i < 100; ++i) {
    EXPECT_EQ(a.NextUint32(1000), b.NextUint32(1000));
  }
}

TEST(RngTest, DifferentSeedsProduceDifferentOutput) {
  Rng a(1);
  Rng b(2);

  bool any_different = false;
  for (int i = 0; i < 100; ++i) {
    if (a.NextUint32(1000000) != b.NextUint32(1000000)) {
      any_different = true;
      break;
    }
  }
  EXPECT_TRUE(any_different);
}

TEST(RngTest, OutputInRange) {
  Rng rng(123);
  for (int i = 0; i < 1000; ++i) {
    uint32_t val = rng.NextUint32(10);
    EXPECT_LT(val, 10u);
  }
}
