/// @file combinatorics.cpp

#include "util/combinatorics.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <type_traits>

namespace coverwise {
namespace util {

std::vector<std::vector<uint32_t>> GenerateCombinations(uint32_t n, uint32_t k) {
  std::vector<std::vector<uint32_t>> result;
  if (k == 0 || k > n) {
    return result;
  }

  uint64_t count = 0;
  if (CheckedBinomial(n, k, BinomialLimit(std::numeric_limits<uint32_t>::max()), count)) {
    result.reserve(static_cast<size_t>(count));
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

std::vector<uint32_t> GenerateCombinationsFlat(uint32_t n, uint32_t k) {
  std::vector<uint32_t> result;
  if (k == 0 || k > n) return result;

  uint64_t count = 0;
  if (CheckedBinomial(n, k, BinomialLimit(std::numeric_limits<uint32_t>::max()), count)) {
    result.reserve(static_cast<size_t>(count) * k);
  }
  std::vector<uint32_t> indices(k);
  for (uint32_t i = 0; i < k; ++i) indices[i] = i;

  while (true) {
    result.insert(result.end(), indices.begin(), indices.end());
    int pos = static_cast<int>(k) - 1;
    while (pos >= 0 && indices[pos] == n - k + static_cast<uint32_t>(pos)) --pos;
    if (pos < 0) break;
    ++indices[pos];
    for (uint32_t j = static_cast<uint32_t>(pos) + 1; j < k; ++j) {
      indices[j] = indices[j - 1] + 1;
    }
  }
  return result;
}

bool CheckedBinomial(uint32_t n, uint32_t k, BinomialLimit limit, uint64_t& result) {
  if (k > n) {
    result = 0;
    return true;
  }
  k = std::min(k, n - k);
  result = 1;

  // The budget is the only thing this refuses on, which is exactly what the
  // TypeScript port refuses on too. That holds because the multiplication below
  // cannot wrap: the budget caps result when each one starts and n caps the
  // numerator, and the two together are narrower than the accumulator. Widening
  // either — or narrowing the accumulator — trips this and reintroduces a wrap
  // the budget comparison would then have to be guarded against separately.
  using LimitValue = decltype(limit.value());
  using Accumulator = std::remove_reference_t<decltype(result)>;
  static_assert(
      std::numeric_limits<LimitValue>::digits + std::numeric_limits<decltype(n)>::digits <=
          std::numeric_limits<Accumulator>::digits,
      "a budget and an n this wide can multiply past the accumulator");

  for (uint32_t i = 1; i <= k; ++i) {
    uint64_t numerator = static_cast<uint64_t>(n - k + i);
    result = (result * numerator) / i;
    if (result > limit.value()) return false;
  }
  return true;
}

void DecodeMixedRadix(uint32_t flat_index, const std::vector<uint32_t>& radixes,
                      std::vector<uint32_t>& out) {
  assert(out.size() == radixes.size());
  uint32_t remainder = flat_index;
  for (int i = static_cast<int>(radixes.size()) - 1; i >= 0; --i) {
    out[i] = remainder % radixes[i];
    remainder /= radixes[i];
  }
}

}  // namespace util
}  // namespace coverwise
