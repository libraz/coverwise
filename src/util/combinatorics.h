/// @file combinatorics.h
/// @brief Combinatorial utility functions.

#ifndef COVERWISE_UTIL_COMBINATORICS_H_
#define COVERWISE_UTIL_COMBINATORICS_H_

#include <cstdint>
#include <vector>

namespace coverwise {
namespace util {

/// @brief Generate all C(n, k) combinations of indices [0, n).
/// @param n The total number of elements.
/// @param k The size of each combination.
/// @return Vector of sorted index vectors. Empty if k == 0 or k > n.
std::vector<std::vector<uint32_t>> GenerateCombinations(uint32_t n, uint32_t k);

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_COMBINATORICS_H_
