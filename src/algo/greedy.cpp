/// @file greedy.cpp

#include "algo/greedy.h"

#include <algorithm>
#include <limits>
#include <numeric>
#include <vector>

namespace coverwise {
namespace algo {

model::TestCase GreedyConstruct(const std::vector<model::Parameter>& params,
                                const core::CoverageEngine& coverage,
                                const std::vector<model::Constraint>& constraints,
                                util::Rng& rng) {
  const auto num_params = static_cast<uint32_t>(params.size());
  constexpr uint32_t kUnassigned = std::numeric_limits<uint32_t>::max();

  // Start with empty (unassigned) test case
  model::TestCase tc;
  tc.values.assign(num_params, kUnassigned);

  // Create a random permutation of parameter indices (Fisher-Yates shuffle)
  std::vector<uint32_t> order(num_params);
  std::iota(order.begin(), order.end(), 0);
  for (uint32_t i = num_params; i > 1; --i) {
    uint32_t j = rng.NextUint32(i);
    std::swap(order[i - 1], order[j]);
  }

  // For each parameter in the permuted order, pick the best value
  for (uint32_t pi : order) {
    uint32_t best_score = 0;
    std::vector<uint32_t> best_values;

    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
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
      tc.values[pi] = kUnassigned;

      if (pruned) {
        continue;
      }

      uint32_t score = coverage.ScoreValue(tc, pi, vi);
      if (best_values.empty() || score > best_score) {
        best_score = score;
        best_values.clear();
        best_values.push_back(vi);
      } else if (score == best_score) {
        best_values.push_back(vi);
      }
    }

    // Pick the chosen value
    if (best_values.empty()) {
      // Fallback: all values pruned by constraints
      tc.values[pi] = 0;
    } else if (best_values.size() == 1) {
      tc.values[pi] = best_values[0];
    } else {
      // Tie-break randomly
      uint32_t idx = rng.NextUint32(static_cast<uint32_t>(best_values.size()));
      tc.values[pi] = best_values[idx];
    }
  }

  return tc;
}

}  // namespace algo
}  // namespace coverwise
