/// @file constraint_solver.h
/// @brief Constraint feasibility search used by the generation core.

#ifndef COVERWISE_CORE_CONSTRAINT_SOLVER_H_
#define COVERWISE_CORE_CONSTRAINT_SOLVER_H_

#include <cstdint>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace core {

/// Default recursion-node budget for a single feasibility search.
///
/// A contradictory or otherwise hard model can make backtracking exponential.
/// Bounding each search to this many nodes guarantees termination; satisfiable
/// models resolve far below the limit under fail-first ordering.
inline constexpr uint64_t kDefaultSolveNodeBudget = 2'000'000;

/// Budget and outcome for a bounded feasibility search.
///
/// When @c remaining reaches zero the search aborts and sets @c exceeded, so the
/// caller can surface an explicit "search budget exceeded" error instead of
/// silently treating the model as unsatisfiable.
struct SolveBudget {
  uint64_t remaining = kDefaultSolveNodeBudget;
  bool exceeded = false;
};

/// Parameter order for a feasibility search, sorted by ascending allowed-domain
/// size. Reusing one order across many partial assignments avoids rescanning
/// every domain at every recursion level.
using SolveParameterOrder = std::vector<uint32_t>;

/// One level of the explicit search stack: the parameter assigned there, the
/// value currently being tried, and the order position the level below starts
/// from.
struct SolveFrame {
  uint32_t param;
  uint32_t value;
  uint32_t next_position;
};

/// Scratch frame stack for a feasibility search.
///
/// The search depth is bounded by the parameter count, so one buffer sized for
/// the model serves every search over it. A caller that solves many partial
/// assignments over the same model -- one per tuple, for instance -- owns a
/// single stack and passes it in, keeping the per-search cost free of heap
/// traffic. The search clears the stack on entry, so its contents never carry
/// between searches and passing a reused stack cannot change any result.
using SolveStack = std::vector<SolveFrame>;

SolveParameterOrder BuildValidSolveParameterOrder(const std::vector<model::Parameter>& params);
SolveParameterOrder BuildAllowedSolveParameterOrder(
    const std::vector<model::Parameter>& params,
    const std::vector<std::vector<bool>>& allowed_values);

/// Complete a partial assignment using the caller-provided allowed-value mask.
///
/// The search is bounded (see SolveBudget). If @p budget is provided and the
/// budget is exhausted, @p budget->exceeded is set and the function returns
/// false; pass nullptr to use a private default budget and ignore the signal.
/// Pass @p stack to reuse one frame buffer across searches; nullptr allocates a
/// private one. The choice affects allocation only, never the outcome.
bool CompleteAssignment(const std::vector<model::Parameter>& params,
                        const std::vector<model::Constraint>& constraints,
                        const std::vector<std::vector<bool>>& allowed_values,
                        model::TestCase& assignment, SolveBudget* budget = nullptr,
                        const SolveParameterOrder* parameter_order = nullptr,
                        SolveStack* stack = nullptr);

/// Complete a partial assignment using valid parameter values.
///
/// The search prunes an assignment as soon as any constraint is definitively
/// false. On success, @p assignment contains a complete satisfying witness.
/// Unassigned entries must use model::kUnassigned. The search is bounded; see
/// CompleteAssignment for the @p budget semantics.
bool CompleteValidAssignment(const std::vector<model::Parameter>& params,
                             const std::vector<model::Constraint>& constraints,
                             model::TestCase& assignment, SolveBudget* budget = nullptr,
                             const SolveParameterOrder* parameter_order = nullptr,
                             SolveStack* stack = nullptr);

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_CONSTRAINT_SOLVER_H_
