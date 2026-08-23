/// @file combinatorics.h
/// @brief Combinatorial utility functions.

#ifndef COVERWISE_UTIL_COMBINATORICS_H_
#define COVERWISE_UTIL_COMBINATORICS_H_

#include <cstdint>
#include <type_traits>
#include <vector>

namespace coverwise {
namespace util {

/// @brief Generate all C(n, k) combinations of indices [0, n).
/// @param n The total number of elements.
/// @param k The size of each combination.
/// @return Vector of sorted index vectors. Empty if k == 0 or k > n.
std::vector<std::vector<uint32_t>> GenerateCombinations(uint32_t n, uint32_t k);

/// @brief Generate all C(n, k) combinations in one contiguous buffer.
///
/// Consecutive groups of @p k entries form one sorted combination. Consumers
/// that ultimately store a flat representation should use this directly to
/// avoid allocating and then copying C(n, k) inner vectors.
std::vector<uint32_t> GenerateCombinationsFlat(uint32_t n, uint32_t k);

/// @brief A combination-count budget for CheckedBinomial, capped at 2^32 - 1.
///
/// The engine ships twice: this core and a TypeScript port that must reach the
/// same verdict for the same query. The cap is what lets both decide it the same
/// way — compare the running count against the budget, stop when it is past.
/// With the budget and n both at most 2^32 - 1, the running count is at most the
/// budget when each multiplication starts and the numerator is at most n, so no
/// intermediate can reach 2^64 or leave the double safe-integer range. Neither
/// implementation needs an arithmetic guard of its own that the other lacks, and
/// there is no second mechanism for the two to disagree about. Above the cap
/// both would need one, and they would not agree on when it fires.
///
/// A budget outside that range is therefore not expressible rather than merely
/// discouraged: nothing but a @c uint32_t reaches the constructor, so a 64-bit,
/// floating-point or signed budget is a compile error instead of a silent
/// conversion. Signed types are refused for the mirror-image reason to wide
/// ones — a negative budget would wrap to 4294967295 and quietly become the most
/// permissive budget there is. A literal therefore carries a @c u suffix.
class BinomialLimit {
 public:
  /// @param value The largest combination count a caller is willing to accept.
  constexpr explicit BinomialLimit(uint32_t value) noexcept : value_(value) {}

  /// @brief Reject any argument that is not already a 32-bit unsigned budget.
  ///
  /// A deleted template rather than a deleted overload per type: overloads are
  /// only ever as complete as the list someone remembered to write, while a
  /// template catches every type the list would have missed. It wins over the
  /// real constructor for those types because it matches them exactly, so
  /// deletion is reached before any conversion can be applied.
  template <typename T, typename = std::enable_if_t<!std::is_same_v<T, uint32_t> &&
                                                    !std::is_same_v<T, BinomialLimit>>>
  BinomialLimit(T) = delete;

  /// @return The budget as a plain 32-bit value.
  constexpr uint32_t value() const noexcept { return value_; }

 private:
  uint32_t value_;
};

/// @brief Compute C(n, k) with a caller-provided safety limit.
/// @return true and writes the exact value when it is <= limit; false when the
///         result exceeds the limit. k > n is answered with true and zero.
bool CheckedBinomial(uint32_t n, uint32_t k, BinomialLimit limit, uint64_t& result);

/// @brief Decode a flat (mixed-radix) index into per-position value indices.
/// @param flat_index The flat index to decode.
/// @param radixes The radix (number of values) for each position, in order.
/// @param out Output vector for decoded indices (must be pre-sized to radixes.size()).
void DecodeMixedRadix(uint32_t flat_index, const std::vector<uint32_t>& radixes,
                      std::vector<uint32_t>& out);

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_COMBINATORICS_H_
