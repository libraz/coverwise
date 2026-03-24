/// @file combinatorics.cpp

#include "util/combinatorics.h"

namespace coverwise {
namespace util {

std::vector<std::vector<uint32_t>> GenerateCombinations(uint32_t n, uint32_t k) {
  std::vector<std::vector<uint32_t>> result;
  if (k == 0 || k > n) {
    return result;
  }

  std::vector<uint32_t> indices(k);
  for (uint32_t i = 0; i < k; ++i) {
    indices[i] = i;
  }

  while (true) {
    result.push_back(indices);

    // Find rightmost index that can be incremented.
    int pos = static_cast<int>(k) - 1;
    while (pos >= 0 && indices[pos] == n - k + static_cast<uint32_t>(pos)) {
      --pos;
    }
    if (pos < 0) break;

    ++indices[pos];
    for (uint32_t j = static_cast<uint32_t>(pos) + 1; j < k; ++j) {
      indices[j] = indices[j - 1] + 1;
    }
  }

  return result;
}

void DecodeMixedRadix(uint32_t flat_index, const std::vector<uint32_t>& radixes,
                      std::vector<uint32_t>& out) {
  uint32_t remainder = flat_index;
  for (int i = static_cast<int>(radixes.size()) - 1; i >= 0; --i) {
    out[i] = remainder % radixes[i];
    remainder /= radixes[i];
  }
}

}  // namespace util
}  // namespace coverwise
