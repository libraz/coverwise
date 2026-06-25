#include "util/rng.h"

#include <gtest/gtest.h>

#include <cstdint>

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

TEST(RngTest, MaxZeroReturnsZero) {
  Rng rng(0);
  EXPECT_EQ(rng.NextUint32(0), 0u);
}

// Pins the xoshiro128** + SplitMix32 integer stream to the TypeScript reference
// in src/ts/util/rng.ts. These exact values must match the TS surface so that
// the same seed produces byte-identical generation across C++/WASM and TS.
TEST(RngTest, MatchesTypeScriptReferenceStream) {
  {
    Rng rng(42);
    const uint32_t expected[] = {924, 897, 282, 142, 180, 290, 186, 688, 214, 302};
    for (uint32_t e : expected) {
      EXPECT_EQ(rng.NextUint32(1000), e);
    }
  }
  {
    Rng rng(123);
    const uint32_t expected[] = {1, 5, 9, 0, 4, 8, 4, 3, 0, 9};
    for (uint32_t e : expected) {
      EXPECT_EQ(rng.NextUint32(10), e);
    }
  }
  {
    Rng rng(7);
    const uint32_t expected[] = {0, 87, 49, 28, 27, 69, 77, 14};
    for (uint32_t e : expected) {
      EXPECT_EQ(rng.NextUint32(100), e);
    }
  }
}

// Reseeding produces the same sequence as a fresh instance.
TEST(RngTest, ReseedMatchesFreshInstance) {
  Rng rng(100);
  for (int i = 0; i < 50; ++i) {
    rng.NextUint32(100);
  }
  rng.Seed(42);
  Rng fresh(42);
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(rng.NextUint32(1000), fresh.NextUint32(1000));
  }
}

// Only the low 32 bits of the seed are significant (canonical seed domain).
TEST(RngTest, SeedUsesLow32Bits) {
  Rng a(42);
  Rng b(static_cast<uint64_t>(42) + (static_cast<uint64_t>(1) << 32));
  for (int i = 0; i < 50; ++i) {
    EXPECT_EQ(a.NextUint32(1000), b.NextUint32(1000));
  }
}
