/// @file tuning_limits.h
/// @brief The internal budgets every surface must agree on, defined once.
///
/// These bound how much work a decision may cost, not what a model may declare.
/// They are tuning values: any of them may be raised or lowered on evidence
/// without that being a change to what coverwise accepts, so they are
/// deliberately kept out of the installed header set. An embedder that could
/// read them could come to depend on them, and retuning would then become a
/// visible change to someone. The acceptance contract lives in limits.h.
///
/// They are here rather than beside their users because the C++ core and the
/// TypeScript port both hold to them, and a budget one surface spends and
/// another does not is the same defect as a limit one accepts and another
/// rejects.

#ifndef COVERWISE_MODEL_TUNING_LIMITS_H_
#define COVERWISE_MODEL_TUNING_LIMITS_H_

#include <cstdint>

namespace coverwise {
namespace model {

/// @brief Maximum number of t-wise tuples any engine will materialize.
///
/// Roughly 16M tuples is about a 2MB bitset; beyond it performance degrades.
/// The generator and the independent validator hold to the same number so a
/// model one of them refuses is not silently accepted by the other.
inline constexpr uint64_t kMaxTuples = 16'000'000;

/// @brief Maximum materialized parameter combinations and lookup-table rows.
inline constexpr uint32_t kMaxCombinations = 1'000'000;

/// @brief Maximum number of human-readable uncovered tuples reported per result.
///
/// A diagnostic bound rather than an acceptance one: exceeding it truncates the
/// list and is reported as an omitted count, never as a rejection.
inline constexpr uint32_t kMaxDiagnosticTuples = 1'000;

/// @brief Recursion-node budget for a single feasibility search.
///
/// Bounds the otherwise exponential backtracking so a hard model terminates; an
/// exhausted budget is reported explicitly rather than being read as
/// "infeasible".
///
/// This is the validator's budget. The solver carries its own, and the two
/// holding the same number is a coincidence of tuning rather than a shared
/// meaning: they bound different searches and either may be retuned alone.
inline constexpr uint64_t kMaxSearchNodes = 2'000'000;

/// @brief Aggregate recursion-node budget for deciding one class tuple.
///
/// Every representative of that tuple draws from this one budget, which is what
/// bounds the representative enumeration itself: a search spends at least one
/// node, so the loop cannot walk a cross product of class members larger than
/// the budget however many values those classes hold. Exhausting it ends the
/// tuple with an explicit budget-exceeded verdict rather than a silent sweep.
///
/// It is a multiple of a single search's budget so that a representative which
/// exhausts its own search does not, on its own, bury a feasible one enumerated
/// after it.
inline constexpr uint64_t kMaxClassTupleSearchNodes = 4 * kMaxSearchNodes;

// Exceeding a single search's budget is what the multiple is for, not a
// coincidence of the number chosen. A class tuple is decided by any one of its
// representatives, so a representative that spends a whole search failing to
// prove itself feasible has to leave budget for the ones enumerated after it.
// At equality the first such representative takes the entire total and the
// tuple comes back undecidable, which makes the verdict depend on the order
// values happen to be declared in rather than on the model.
static_assert(kMaxClassTupleSearchNodes > kMaxSearchNodes,
              "one class tuple must afford more than a single full search");

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_TUNING_LIMITS_H_
