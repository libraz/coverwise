/// @file allocation_counter.cpp
/// @brief Global allocation-function replacements backing AllocationCounter.

#include "support/allocation_counter.h"

#include <cstdint>
#include <cstdlib>
#include <new>

namespace {

bool g_counting_allocations = false;
uint64_t g_allocation_count = 0;

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

namespace coverwise {
namespace test_support {

AllocationCounter::AllocationCounter() {
  g_allocation_count = 0;
  g_counting_allocations = true;
}

AllocationCounter::~AllocationCounter() { g_counting_allocations = false; }

uint64_t AllocationCounter::Stop() {
  g_counting_allocations = false;
  return g_allocation_count;
}

}  // namespace test_support
}  // namespace coverwise

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
// Only the plain scalar operator new touches the counter; see the header for
// why that is the form the assertions are written against.

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
