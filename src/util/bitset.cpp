/// @file bitset.cpp
/// @brief Dynamic bitset implementation.

#include "util/bitset.h"

namespace coverwise {
namespace util {

DynamicBitset::DynamicBitset(uint32_t num_bits)
    : num_bits_(num_bits), blocks_(NumBlocks(num_bits), 0) {}

void DynamicBitset::Set(uint32_t index) {
  blocks_[index / kBitsPerBlock] |= (1ULL << (index % kBitsPerBlock));
}

void DynamicBitset::Clear(uint32_t index) {
  blocks_[index / kBitsPerBlock] &= ~(1ULL << (index % kBitsPerBlock));
}

bool DynamicBitset::Test(uint32_t index) const {
  return (blocks_[index / kBitsPerBlock] & (1ULL << (index % kBitsPerBlock))) != 0;
}

uint32_t DynamicBitset::Count() const {
  uint32_t count = 0;
  for (auto block : blocks_) {
    count += static_cast<uint32_t>(__builtin_popcountll(block));
  }
  return count;
}

uint32_t DynamicBitset::CountAndNot(const DynamicBitset& other) const {
  uint32_t count = 0;
  for (size_t i = 0; i < blocks_.size(); ++i) {
    count += static_cast<uint32_t>(__builtin_popcountll(blocks_[i] & ~other.blocks_[i]));
  }
  return count;
}

void DynamicBitset::UnionWith(const DynamicBitset& other) {
  for (size_t i = 0; i < blocks_.size(); ++i) {
    blocks_[i] |= other.blocks_[i];
  }
}

void DynamicBitset::Reset() { std::fill(blocks_.begin(), blocks_.end(), 0); }

uint32_t DynamicBitset::NumBlocks(uint32_t num_bits) {
  return (num_bits + kBitsPerBlock - 1) / kBitsPerBlock;
}

}  // namespace util
}  // namespace coverwise
