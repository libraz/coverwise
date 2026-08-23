/// @file greedy.cpp

#include "algo/greedy.h"

#include <algorithm>
#include <vector>

namespace coverwise {
namespace algo {

void GreedyScratch::Reserve(const std::vector<model::Parameter>& params) {
  order.reserve(params.size());
  uint32_t max_values = 0;
  for (const auto& param : params) {
    max_values = std::max(max_values, param.size());
  }
  scores.reserve(max_values);
  best_values.reserve(max_values);
}

namespace detail {

uint32_t BreakTieWithWeights(const std::vector<uint32_t>& best_values,
                             const std::vector<std::vector<double>>& weights, uint32_t pi,
                             util::Rng& rng) {
  if (best_values.size() == 1) {
    return best_values[0];
  }
  if (!weights.empty()) {
    // Normalize by the largest weight first so finite inputs cannot overflow
    // their sum (e.g. two DBL_MAX weights).
    double max_weight = 0.0;
    for (uint32_t vi : best_values) max_weight = std::max(max_weight, weights[pi][vi]);
    double total_weight = 0.0;
    if (max_weight > 0.0) {
      for (uint32_t vi : best_values) {
        total_weight += weights[pi][vi] / max_weight;
      }
    }
    if (total_weight > 0.0) {
      // Generate a random value in [0, total_weight).
      double r = static_cast<double>(rng.NextUint32(1000000)) / 1000000.0 * total_weight;
      double cumulative = 0.0;
      for (uint32_t vi : best_values) {
        cumulative += weights[pi][vi] / max_weight;
        if (r < cumulative) {
          return vi;
        }
      }
      // Fallback to last value (floating point edge case).
      return best_values.back();
    }
  }
  // No weights or zero total: random tie-break.
  uint32_t idx = rng.NextUint32(static_cast<uint32_t>(best_values.size()));
  return best_values[idx];
}

}  // namespace detail
}  // namespace algo
}  // namespace coverwise
