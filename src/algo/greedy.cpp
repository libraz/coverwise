/// @file greedy.cpp

#include "algo/greedy.h"

#include <algorithm>
#include <numeric>
#include <vector>

namespace coverwise {
namespace algo {

namespace {

/// @brief Break ties among best_values using weights, then RNG for remaining ties.
///
/// Uses weighted random selection: each tied value's probability is proportional
/// to its weight. This biases toward higher-weighted values while maintaining
/// enough randomness for the greedy algorithm to explore diverse test cases.
/// @return The chosen value index.
uint32_t BreakTieWithWeights(const std::vector<uint32_t>& best_values,
                             const std::vector<std::vector<double>>& weights, uint32_t pi,
                             util::Rng& rng) {
  if (best_values.size() == 1) {
    return best_values[0];
  }
  if (!weights.empty()) {
    // Weighted random selection: probability proportional to weight.
    double total_weight = 0.0;
    for (uint32_t vi : best_values) {
      total_weight += weights[pi][vi];
    }
    if (total_weight > 0.0) {
      // Generate a random value in [0, total_weight).
      double r = static_cast<double>(rng.NextUint32(1000000)) / 1000000.0 * total_weight;
      double cumulative = 0.0;
      for (uint32_t vi : best_values) {
        cumulative += weights[pi][vi];
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

}  // namespace

model::TestCase GreedyConstruct(const std::vector<model::Parameter>& params, ScoreFn score_fn,
                                const std::vector<model::Constraint>& constraints, util::Rng& rng,
                                const std::vector<std::vector<bool>>& allowed_values,
                                const std::vector<std::vector<double>>& weights) {
  const auto num_params = static_cast<uint32_t>(params.size());

  model::TestCase tc;
  tc.values.assign(num_params, model::kUnassigned);

  // Fisher-Yates shuffle for parameter order
  std::vector<uint32_t> order(num_params);
  std::iota(order.begin(), order.end(), 0);
  for (uint32_t i = num_params; i > 1; --i) {
    uint32_t j = rng.NextUint32(i);
    std::swap(order[i - 1], order[j]);
  }

  for (uint32_t pi : order) {
    uint32_t best_score = 0;
    std::vector<uint32_t> best_values;

    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (!allowed_values.empty() && !allowed_values[pi][vi]) continue;

      // Temporarily assign value for constraint evaluation
      tc.values[pi] = vi;

      // Evaluate constraints using three-valued logic
      bool pruned = false;
      for (const auto& constraint : constraints) {
        auto result = constraint->Evaluate(tc.values);
        if (result == model::ConstraintResult::kFalse) {
          pruned = true;
          break;
        }
        // kTrue and kUnknown: continue
      }

      // Reset before deciding
      tc.values[pi] = model::kUnassigned;

      if (pruned) continue;

      uint32_t score = score_fn(tc, pi, vi);
      if (best_values.empty() || score > best_score) {
        best_score = score;
        best_values.clear();
        best_values.push_back(vi);
      } else if (score == best_score) {
        best_values.push_back(vi);
      }
    }

    if (best_values.empty()) {
      // Fallback: pick the first allowed value that satisfies constraints.
      uint32_t fallback = model::kUnassigned;
      for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
        if (!allowed_values.empty() && !allowed_values[pi][vi]) continue;

        tc.values[pi] = vi;
        bool pruned = false;
        for (const auto& constraint : constraints) {
          auto result = constraint->Evaluate(tc.values);
          if (result == model::ConstraintResult::kFalse) {
            pruned = true;
            break;
          }
        }
        tc.values[pi] = model::kUnassigned;

        if (!pruned) {
          fallback = vi;
          break;
        }
      }
      if (fallback != model::kUnassigned) {
        tc.values[pi] = fallback;
      } else if (!allowed_values.empty()) {
        // All allowed values violate constraints; pick first allowed as last resort.
        for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
          if (allowed_values[pi][vi]) {
            tc.values[pi] = vi;
            break;
          }
        }
        if (tc.values[pi] == model::kUnassigned) {
          tc.values[pi] = 0;
        }
      } else {
        tc.values[pi] = 0;
      }
    } else {
      tc.values[pi] = BreakTieWithWeights(best_values, weights, pi, rng);
    }
  }

  return tc;
}

}  // namespace algo
}  // namespace coverwise
