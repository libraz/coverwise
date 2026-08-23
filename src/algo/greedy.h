/// @file greedy.h
/// @brief Constructive greedy algorithm for covering array construction.
///
/// Instead of enumerating all candidates (combinatorial explosion),
/// builds test cases parameter-by-parameter, choosing the value that
/// maximizes coverage gain at each step. This is O(n * max_values)
/// per test case instead of O(Π values).

#ifndef COVERWISE_ALGO_GREEDY_H_
#define COVERWISE_ALGO_GREEDY_H_

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <optional>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "util/rng.h"

namespace coverwise {
namespace algo {

/// @brief Buffers a greedy construction borrows instead of allocating its own.
///
/// One instance is owned by the generation pass and handed to every
/// GreedyConstruct() call, so the heap traffic of building a suite is bounded by
/// the widest single parameter rather than growing with the number of test cases
/// or parameters. The contents carry no meaning between calls.
struct GreedyScratch {
  std::vector<uint32_t> order;        ///< Parameter order, reshuffled per construction.
  std::vector<uint32_t> scores;       ///< Coverage gain per value of the current parameter.
  std::vector<uint32_t> best_values;  ///< Value indices tied for the best score.

  /// @brief Size the buffers for @p params so no later construction grows them.
  void Reserve(const std::vector<model::Parameter>& params);
};

/// @brief A constructed test case together with the coverage it gains.
struct GreedyResult {
  model::TestCase test_case;
  /// @brief Newly covered tuples, accumulated while the test case was built.
  ///
  /// Each parameter combination is scored exactly once — at the step that
  /// assigns the last of its parameters — and the coverage state does not change
  /// during a construction, so this equals scoring the finished test case
  /// against that same state.
  uint32_t score = 0;
};

namespace detail {

/// @brief Break ties among @p best_values using weights, then RNG for the rest.
///
/// Uses weighted random selection: each tied value's probability is proportional
/// to its weight. This biases toward higher-weighted values while maintaining
/// enough randomness for the greedy algorithm to explore diverse test cases.
/// @return The chosen value index.
uint32_t BreakTieWithWeights(const std::vector<uint32_t>& best_values,
                             const std::vector<std::vector<double>>& weights, uint32_t pi,
                             util::Rng& rng);

}  // namespace detail

/// @brief Build a test case parameter-by-parameter using greedy value selection.
///
/// For each parameter (in order), evaluate all possible values and pick
/// the one that would cover the most uncovered tuples. Ties broken by RNG.
///
/// Constraints are evaluated at each step using three-valued logic:
/// - true: continue
/// - false: skip this value (prune)
/// - unknown: continue (not all params assigned yet)
///
/// This is a single-pass construction: once a parameter is assigned it is never
/// revisited (no backtracking). Under adversarial constraints a locally-greedy
/// choice can therefore leave some satisfiable tuples uncovered. This is a
/// deliberate approximation in favour of speed; the caller bounds the number of
/// retries and reports any resulting shortfall (coverage < 1.0 plus a warning)
/// rather than guaranteeing optimal coverage.
///
/// @param params Parameter definitions.
/// @param score_values Callback invoked as
///   `score_values(const model::TestCase& partial, uint32_t param_index, uint32_t* out_scores)`,
///   which adds the coverage gain of every value of @p param_index into
///   `out_scores[value_index]`. Scoring a whole parameter at once keeps the work
///   per candidate value O(1); scoring values one at a time repeats the sweep
///   over the parameter's combinations for every value. Taken as a template
///   parameter so the call is not routed through a type-erasing wrapper.
/// @param constraints Active constraints (empty if none).
/// @param rng Random number generator for tie-breaking and parameter ordering.
/// @param scratch Caller-owned buffers, reused across constructions.
/// @param allowed_values Optional per-parameter mask of allowed values.
///   If non-empty, allowed_values[pi][vi] must be true for value vi of param pi
///   to be considered. If empty, all values are allowed.
/// @param weights Optional per-parameter per-value weights for tie-breaking.
///   If non-empty, weights[pi][vi] is the weight for value vi of param pi.
///   When multiple values tie on coverage score, one is chosen by weighted
///   random selection: the probability of picking a value is proportional to its
///   weight (a higher weight makes a value more likely, not certain). Default
///   weight is 1.0, which reduces to a uniform random tie-break.
/// @return The constructed test case and its coverage gain, or std::nullopt if
///   no constraint-satisfying value exists for some parameter. A
///   constraint-violating value is never written into the returned test case.
template <typename ScoreValuesFn>
std::optional<GreedyResult> GreedyConstruct(
    const std::vector<model::Parameter>& params, ScoreValuesFn&& score_values,
    const std::vector<model::Constraint>& constraints, util::Rng& rng, GreedyScratch& scratch,
    const std::vector<std::vector<bool>>& allowed_values = {},
    const std::vector<std::vector<double>>& weights = {}) {
  const auto num_params = static_cast<uint32_t>(params.size());

  GreedyResult result;
  model::TestCase& tc = result.test_case;
  tc.values.assign(num_params, model::kUnassigned);

  // Fisher-Yates shuffle for parameter order
  scratch.order.resize(num_params);
  std::iota(scratch.order.begin(), scratch.order.end(), 0u);
  for (uint32_t i = num_params; i > 1; --i) {
    uint32_t j = rng.NextUint32(i);
    std::swap(scratch.order[i - 1], scratch.order[j]);
  }

  // Single-pass, no-backtracking construction: each parameter is assigned once in
  // shuffled order. This is a deliberate approximation favouring speed — a greedy
  // local choice may leave some satisfiable tuples uncovered, which the caller
  // surfaces as coverage < 1.0 rather than retrying exhaustively.
  for (uint32_t pi : scratch.order) {
    const uint32_t num_values = params[pi].size();
    scratch.scores.assign(num_values, 0);
    score_values(tc, pi, scratch.scores.data());

    uint32_t best_score = 0;
    scratch.best_values.clear();

    for (uint32_t vi = 0; vi < num_values; ++vi) {
      if (!allowed_values.empty() && !allowed_values[pi][vi]) continue;

      // Temporarily assign value for constraint evaluation
      tc.values[pi] = vi;

      // Evaluate constraints using three-valued logic
      bool pruned = false;
      for (const auto& constraint : constraints) {
        auto evaluated = constraint->Evaluate(tc.values);
        if (evaluated == model::ConstraintResult::kFalse) {
          pruned = true;
          break;
        }
        // kTrue and kUnknown: continue
      }

      // Reset before deciding
      tc.values[pi] = model::kUnassigned;

      if (pruned) continue;

      uint32_t score = scratch.scores[vi];
      if (scratch.best_values.empty() || score > best_score) {
        best_score = score;
        scratch.best_values.clear();
        scratch.best_values.push_back(vi);
      } else if (score == best_score) {
        scratch.best_values.push_back(vi);
      }
    }

    // Every value allowed by the mask that survives the constraints is recorded
    // above, so an empty set means this parameter has no usable value at all.
    // Constraint evaluation is a pure function of the same partial assignment,
    // so no second pass could reach a different verdict.
    if (scratch.best_values.empty()) return std::nullopt;

    tc.values[pi] = detail::BreakTieWithWeights(scratch.best_values, weights, pi, rng);
    result.score += best_score;
  }

  return result;
}

}  // namespace algo
}  // namespace coverwise

#endif  // COVERWISE_ALGO_GREEDY_H_
