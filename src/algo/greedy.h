/// @file greedy.h
/// @brief Constructive greedy algorithm for covering array construction.
///
/// Instead of enumerating all candidates (combinatorial explosion),
/// builds test cases parameter-by-parameter, choosing the value that
/// maximizes coverage gain at each step. This is O(n * max_values)
/// per test case instead of O(Π values).

#ifndef COVERWISE_ALGO_GREEDY_H_
#define COVERWISE_ALGO_GREEDY_H_

#include <cstdint>
#include <vector>

#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "util/rng.h"

namespace coverwise {
namespace algo {

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
/// @param params Parameter definitions.
/// @param coverage Current coverage state.
/// @param constraints Active constraints (empty if none).
/// @param rng Random number generator for tie-breaking and parameter ordering.
/// @param allowed_values Optional per-parameter mask of allowed values.
///   If non-empty, allowed_values[pi][vi] must be true for value vi of param pi
///   to be considered. If empty, all values are allowed.
/// @param weights Optional per-parameter per-value weights for tie-breaking.
///   If non-empty, weights[pi][vi] is the weight for value vi of param pi.
///   When multiple values tie on coverage score, the highest-weighted one is
///   preferred. Default weight is 1.0.
/// @return The constructed test case.
model::TestCase GreedyConstruct(const std::vector<model::Parameter>& params,
                                const core::CoverageEngine& coverage,
                                const std::vector<model::Constraint>& constraints, util::Rng& rng,
                                const std::vector<std::vector<bool>>& allowed_values = {},
                                const std::vector<std::vector<double>>& weights = {});

/// @brief Build a test case using greedy value selection with multiple engines.
///
/// Scores are summed across all coverage engines. Used for mixed-strength
/// (sub-model) generation where different parameter groups have different
/// coverage requirements.
///
/// @param params Parameter definitions.
/// @param engines Coverage engines (global + sub-model engines).
/// @param constraints Active constraints (empty if none).
/// @param rng Random number generator for tie-breaking and parameter ordering.
/// @param weights Optional per-parameter per-value weights for tie-breaking.
/// @return The constructed test case.
model::TestCase GreedyConstruct(const std::vector<model::Parameter>& params,
                                const std::vector<const core::CoverageEngine*>& engines,
                                const std::vector<model::Constraint>& constraints, util::Rng& rng,
                                const std::vector<std::vector<double>>& weights = {});

}  // namespace algo
}  // namespace coverwise

#endif  // COVERWISE_ALGO_GREEDY_H_
