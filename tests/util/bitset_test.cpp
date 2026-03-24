#include "util/bitset.h"

#include <gtest/gtest.h>

using coverwise::util::DynamicBitset;

TEST(DynamicBitsetTest, InitiallyAllClear) {
  DynamicBitset bs(128);
  EXPECT_EQ(bs.Count(), 0u);
  EXPECT_EQ(bs.Size(), 128u);
}

TEST(DynamicBitsetTest, SetAndTest) {
  DynamicBitset bs(100);
  bs.Set(0);
  bs.Set(42);
  bs.Set(99);

  EXPECT_TRUE(bs.Test(0));
  EXPECT_TRUE(bs.Test(42));
  EXPECT_TRUE(bs.Test(99));
  EXPECT_FALSE(bs.Test(1));
  EXPECT_FALSE(bs.Test(98));
}

TEST(DynamicBitsetTest, Count) {
  DynamicBitset bs(200);
  bs.Set(10);
  bs.Set(100);
  bs.Set(199);
  EXPECT_EQ(bs.Count(), 3u);
}

TEST(DynamicBitsetTest, Clear) {
  DynamicBitset bs(64);
  bs.Set(30);
  EXPECT_TRUE(bs.Test(30));

  bs.Clear(30);
  EXPECT_FALSE(bs.Test(30));
  EXPECT_EQ(bs.Count(), 0u);
}

TEST(DynamicBitsetTest, CountAndNot) {
  DynamicBitset a(100);
  DynamicBitset b(100);

  a.Set(1);
  a.Set(2);
  a.Set(3);

  b.Set(2);

  // a AND NOT b = {1, 3}
  EXPECT_EQ(a.CountAndNot(b), 2u);
}

TEST(DynamicBitsetTest, UnionWith) {
  DynamicBitset a(100);
  DynamicBitset b(100);

  a.Set(1);
  b.Set(2);

  a.UnionWith(b);
  EXPECT_TRUE(a.Test(1));
  EXPECT_TRUE(a.Test(2));
  EXPECT_EQ(a.Count(), 2u);
}

TEST(DynamicBitsetTest, Reset) {
  DynamicBitset bs(64);
  bs.Set(0);
  bs.Set(63);
  EXPECT_EQ(bs.Count(), 2u);

  bs.Reset();
  EXPECT_EQ(bs.Count(), 0u);
}

TEST(DynamicBitsetTest, EmptyBitset) {
  DynamicBitset bs(0);
  EXPECT_EQ(bs.Size(), 0u);
  EXPECT_EQ(bs.Count(), 0u);
}

TEST(DynamicBitsetTest, CrossBlockBoundary) {
  DynamicBitset bs(128);
  bs.Set(63);  // Last bit of first block
  bs.Set(64);  // First bit of second block
  EXPECT_TRUE(bs.Test(63));
  EXPECT_TRUE(bs.Test(64));
  EXPECT_EQ(bs.Count(), 2u);
}
