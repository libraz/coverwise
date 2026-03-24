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

/// @brief Decode a flat (mixed-radix) index into per-position value indices.
/// @param flat_index The flat index to decode.
/// @param radixes The radix (number of values) for each position, in order.
/// @param out Output vector for decoded indices (must be pre-sized to radixes.size()).
void DecodeMixedRadix(uint32_t flat_index, const std::vector<uint32_t>& radixes,
                      std::vector<uint32_t>& out);

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_COMBINATORICS_H_
