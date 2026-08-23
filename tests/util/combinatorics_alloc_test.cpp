/// @file combinatorics_alloc_test.cpp
/// @brief Heap-allocation behaviour of the combination generators.

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <vector>

#include "support/allocation_counter.h"
#include "util/combinatorics.h"

using coverwise::test_support::AllocationCounter;
using coverwise::util::BinomialLimit;
using coverwise::util::CheckedBinomial;
using coverwise::util::GenerateCombinations;
using coverwise::util::GenerateCombinationsFlat;

TEST(CombinatoricsAllocationTest, TheNestedGeneratorAllocatesOncePerEmittedCombination) {
  // C(30, 5) = 142506, past the hundred-thousand mark where an outer vector
  // that grows by doubling would rebuild its buffer around twenty times.
  constexpr uint32_t kN = 30;
  constexpr uint32_t kK = 5;
  uint64_t expected_count = 0;
  ASSERT_TRUE(
      CheckedBinomial(kN, kK, BinomialLimit(std::numeric_limits<uint32_t>::max()), expected_count));
  ASSERT_GT(expected_count, 100000u);

  AllocationCounter counter;
  auto combinations = GenerateCombinations(kN, kK);
  const uint64_t allocations = counter.Stop();

  ASSERT_EQ(combinations.size(), expected_count);
  // One buffer for the index scratch, one for the outer vector, and one per
  // emitted combination. The count is exactly computable up front, so any
  // allocation beyond that is the outer vector reallocating.
  EXPECT_EQ(allocations, expected_count + 2);
}

TEST(CombinatoricsAllocationTest, TheFlatGeneratorAllocatesTwoBuffers) {
  constexpr uint32_t kN = 30;
  constexpr uint32_t kK = 5;
  uint64_t expected_count = 0;
  ASSERT_TRUE(
      CheckedBinomial(kN, kK, BinomialLimit(std::numeric_limits<uint32_t>::max()), expected_count));

  AllocationCounter counter;
  auto flat = GenerateCombinationsFlat(kN, kK);
  const uint64_t allocations = counter.Stop();

  ASSERT_EQ(flat.size(), expected_count * kK);
  // The index scratch and the single reserved output buffer.
  EXPECT_EQ(allocations, 2u);
}

TEST(CombinatoricsAllocationTest, AnUncomputableCountStillProducesEveryCombination) {
  // k > n and k == 0 return early, before the count is ever needed.
  EXPECT_TRUE(GenerateCombinations(3, 4).empty());
  EXPECT_TRUE(GenerateCombinations(3, 0).empty());
  EXPECT_EQ(GenerateCombinations(4, 4).size(), 1u);
  EXPECT_EQ(GenerateCombinations(5, 2).size(), 10u);
}
