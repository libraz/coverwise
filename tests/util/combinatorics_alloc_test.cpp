/// @file combinatorics_alloc_test.cpp
/// @brief Heap-allocation behaviour of the combination generators.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

#include "util/combinatorics.h"

using coverwise::util::BinomialLimit;
using coverwise::util::CheckedBinomial;
using coverwise::util::GenerateCombinations;
using coverwise::util::GenerateCombinationsFlat;

namespace {

// Heap-allocation instrumentation. Counting is off unless a scope turns it on,
// and it exists only in this test binary -- the library is built without it, so
// a release build carries no instrumentation at all.
bool g_counting_allocations = false;
uint64_t g_allocation_count = 0;

class AllocationCounter {
 public:
  AllocationCounter() {
    g_allocation_count = 0;
    g_counting_allocations = true;
  }
  ~AllocationCounter() { g_counting_allocations = false; }
  AllocationCounter(const AllocationCounter&) = delete;
  AllocationCounter& operator=(const AllocationCounter&) = delete;

  uint64_t Stop() {
    g_counting_allocations = false;
    return g_allocation_count;
  }
};

/// @brief Allocates an unaligned block, never returning a zero-sized one.
void* Allocate(size_t size) { return std::malloc(size == 0 ? 1 : size); }

/// @brief Allocates a block honouring an over-aligned type's alignment.
///
/// std::aligned_alloc requires the size to be a multiple of the alignment, so
/// the request is rounded up before the block is asked for; the surplus is part
/// of the same block and is released by the same std::free.
void* AlignedAllocate(size_t size, size_t alignment) {
  if (alignment < sizeof(void*)) alignment = sizeof(void*);
  const size_t requested = size == 0 ? 1 : size;
  const size_t rounded = (requested + alignment - 1) / alignment * alignment;
  return std::aligned_alloc(alignment, rounded);
}

}  // namespace

// Replacing the global allocation functions is all-or-nothing. Every
// deallocation function below releases with std::free, so it may only ever be
// handed memory that an allocation function below produced. Leaving any single
// form to the implementation -- an array form, a nothrow form, an over-aligned
// form -- lets that form hand out a block the implementation owns which is then
// released through std::free: undefined behaviour, and what AddressSanitizer
// reports as alloc-dealloc-mismatch. libstdc++'s std::get_temporary_buffer, for
// one, allocates through the nothrow scalar form and releases through sized
// delete. Add to or remove from this family as a whole, never one function at a
// time.
//
// Only the plain scalar operator new touches the counter: the generators under
// test allocate through std::vector, and the assertions are written against
// that count.

void* operator new(size_t size) {
  if (g_counting_allocations) ++g_allocation_count;
  void* memory = Allocate(size);
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](size_t size) {
  void* memory = Allocate(size);
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new(size_t size, const std::nothrow_t&) noexcept { return Allocate(size); }
void* operator new[](size_t size, const std::nothrow_t&) noexcept { return Allocate(size); }

void* operator new(size_t size, std::align_val_t alignment) {
  void* memory = AlignedAllocate(size, static_cast<size_t>(alignment));
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new[](size_t size, std::align_val_t alignment) {
  void* memory = AlignedAllocate(size, static_cast<size_t>(alignment));
  if (memory == nullptr) throw std::bad_alloc();
  return memory;
}

void* operator new(size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return AlignedAllocate(size, static_cast<size_t>(alignment));
}

void* operator new[](size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
  return AlignedAllocate(size, static_cast<size_t>(alignment));
}

void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t) noexcept { std::free(memory); }
void operator delete(void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete[](void* memory, const std::nothrow_t&) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete[](void* memory, size_t, std::align_val_t) noexcept { std::free(memory); }
void operator delete(void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(memory);
}
void operator delete[](void* memory, std::align_val_t, const std::nothrow_t&) noexcept {
  std::free(memory);
}

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
