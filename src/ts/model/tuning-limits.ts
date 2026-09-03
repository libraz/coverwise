/**
 * The internal budgets every TypeScript surface must agree on, defined once.
 *
 * These bound how much work a decision may cost, not what a model may declare.
 * They are tuning values: any of them may be raised or lowered on evidence
 * without that being a change to what coverwise accepts, so they are kept apart
 * from the acceptance contract in `limits.ts` and out of every entry point's
 * exports. This module is the TypeScript mirror of `src/model/tuning_limits.h`,
 * and `limits.test.ts` reads that header and asserts each constant matches.
 */

/**
 * Maximum number of t-wise tuples any engine will materialize.
 *
 * Roughly 16M tuples is about a 2MB bitset; beyond it performance degrades.
 * The generator and the independent validator hold to the same number so a
 * model one of them refuses is not silently accepted by the other.
 */
export const MAX_TUPLES = 16_000_000;

/** Maximum materialized parameter combinations and lookup-table rows. */
export const MAX_COMBINATIONS = 1_000_000;

/**
 * Maximum number of human-readable uncovered tuples reported per result.
 *
 * A diagnostic bound rather than an acceptance one: exceeding it truncates the
 * list and is reported as an omitted count, never as a rejection.
 */
export const MAX_DIAGNOSTIC_TUPLES = 1_000;

/**
 * Recursion-node budget for a single feasibility search.
 *
 * Bounds the otherwise exponential backtracking so a hard model terminates; an
 * exhausted budget is reported explicitly rather than being read as
 * "infeasible".
 *
 * This is the validator's budget. The solver carries its own, and the two
 * holding the same number is a coincidence of tuning rather than a shared
 * meaning: they bound different searches and either may be retuned alone.
 */
export const MAX_SEARCH_NODES = 2_000_000;

/**
 * Aggregate recursion-node budget for deciding one class tuple.
 *
 * Every representative of that tuple draws from this one budget, which is what
 * bounds the representative enumeration itself: a search spends at least one
 * node, so the loop cannot walk a cross product of class members larger than
 * the budget however many values those classes hold. Exhausting it ends the
 * tuple with an explicit budget-exceeded verdict rather than a silent sweep.
 *
 * It is a multiple of a single search's budget so that a representative which
 * exhausts its own search does not, on its own, bury a feasible one enumerated
 * after it.
 *
 * Exceeding MAX_SEARCH_NODES is required, not a coincidence of the number
 * chosen: at equality the first representative to spend a whole search takes
 * the entire total and the tuple comes back undecidable, which makes the
 * verdict depend on the order values happen to be declared in rather than on
 * the model. The C++ side holds this with a static_assert at the definition;
 * TypeScript cannot state it there, so the order-independence test in the
 * validator's suite is what reports it, and it names this constant when it
 * fails.
 */
export const MAX_CLASS_TUPLE_SEARCH_NODES = 4 * MAX_SEARCH_NODES;
