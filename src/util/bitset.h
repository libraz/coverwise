/// @file bitset.h
/// @brief Dynamic bitset for coverage tracking.

#ifndef COVERWISE_UTIL_BITSET_H_
#define COVERWISE_UTIL_BITSET_H_

#include <cstdint>
#include <vector>

namespace coverwise {
namespace util {

/// @brief A dynamic bitset optimized for coverage tracking.
///
/// Uses uint64_t blocks for efficient bulk operations (AND, OR, popcount).
class DynamicBitset {
 public:
  /// @brief Construct a bitset with the given number of bits, all cleared.
  explicit DynamicBitset(uint32_t num_bits = 0);

  /// @brief Set the bit at the given index.
  void Set(uint32_t index);

  /// @brief Clear the bit at the given index.
  void Clear(uint32_t index);

  /// @brief Test whether the bit at the given index is set.
  bool Test(uint32_t index) const;

  /// @brief Count the number of set bits.
  uint32_t Count() const;

  /// @brief Return the total number of bits.
  uint32_t Size() const { return num_bits_; }

  /// @brief Count bits set in (this AND NOT other).
  uint32_t CountAndNot(const DynamicBitset& other) const;

  /// @brief Set all bits from other into this (this |= other).
  void UnionWith(const DynamicBitset& other);

  /// @brief Clear all bits.
  void Reset();

 private:
  uint32_t num_bits_ = 0;
  std::vector<uint64_t> blocks_;

  static constexpr uint32_t kBitsPerBlock = 64;
  static uint32_t NumBlocks(uint32_t num_bits);
};

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_BITSET_H_
