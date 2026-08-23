/// @file allocation_counter.h
/// @brief Scoped counting of heap allocations made by the code under test.

#ifndef COVERWISE_TESTS_SUPPORT_ALLOCATION_COUNTER_H_
#define COVERWISE_TESTS_SUPPORT_ALLOCATION_COUNTER_H_

#include <cstdint>

namespace coverwise {
namespace test_support {

/// @brief Counts plain scalar operator new calls for the lifetime of a scope.
///
/// Counting is off until a counter is constructed, and it exists only in the
/// test binaries that link allocation_counter.cpp -- the library is built
/// without it, so a release build carries no instrumentation at all.
///
/// Only the plain scalar operator new is counted: the code under test allocates
/// through std::vector, and the assertions are written against that count.
class AllocationCounter {
 public:
  /// @brief Resets the count to zero and starts counting.
  AllocationCounter();
  ~AllocationCounter();
  AllocationCounter(const AllocationCounter&) = delete;
  AllocationCounter& operator=(const AllocationCounter&) = delete;

  /// @brief Stops counting and returns the allocations seen since construction.
  uint64_t Stop();
};

}  // namespace test_support
}  // namespace coverwise

#endif  // COVERWISE_TESTS_SUPPORT_ALLOCATION_COUNTER_H_
