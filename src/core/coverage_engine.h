/// @file coverage_engine.h
/// @brief Coverage tracking engine for t-wise tuple coverage.

#ifndef COVERWISE_CORE_COVERAGE_ENGINE_H_
#define COVERWISE_CORE_COVERAGE_ENGINE_H_

#include <cassert>
#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "model/tuning_limits.h"
#include "util/bitset.h"
#include "util/combinatorics.h"

namespace coverwise {
namespace core {

/// @brief Tracks which t-wise tuples are covered by the current test suite.
///
/// Hard limit on total tuple count to prevent t-wise explosion.
/// For large t-wise, consider sparse mode (future).
class CoverageEngine {
 public:
  /// @brief An immutable parameter set shared by a family of engines.
  ///
  /// An engine only ever reads its parameters, so several engines over the same
  /// model can share one copy. Both whole-model and subset engines index the
  /// full parameter set by global index, so a sub-model engine covering three
  /// parameters still needs the whole set available; sharing keeps that from
  /// duplicating the model's string payload once per engine.
  using SharedParameters = std::shared_ptr<const std::vector<model::Parameter>>;

  /// @brief Wrap a parameter set so several engines can share one copy.
  static SharedParameters ShareParameters(std::vector<model::Parameter> params) {
    return std::make_shared<const std::vector<model::Parameter>>(std::move(params));
  }

  /// @brief Initialize coverage tracking for the given parameters and strength.
  /// @param params The parameter definitions.
  /// @param strength The interaction strength (t). 2 = pairwise.
  /// @return Error if tuple count exceeds model::kMaxTuples.
  static std::pair<CoverageEngine, model::Error> Create(const std::vector<model::Parameter>& params,
                                                        uint32_t strength) {
    return CreateShared(ShareParameters(params), strength);
  }

  /// @brief Initialize coverage tracking for a subset of parameters.
  ///
  /// Only the parameters at the given indices are considered for tuple
  /// generation. Test cases still use global parameter indices.
  /// @param all_params All parameter definitions.
  /// @param param_subset Indices of parameters to cover (must be sorted).
  /// @param strength The interaction strength (t).
  /// @return Error if tuple count exceeds model::kMaxTuples.
  static std::pair<CoverageEngine, model::Error> Create(
      const std::vector<model::Parameter>& all_params, const std::vector<uint32_t>& param_subset,
      uint32_t strength) {
    return CreateShared(ShareParameters(all_params), param_subset, strength);
  }

  /// @brief Initialize coverage tracking over an already-shared parameter set.
  ///
  /// Building the global engine and one engine per sub-model from a single
  /// SharedParameters stores the model's strings once for all of them.
  /// @see Create for the parameter and error semantics.
  static std::pair<CoverageEngine, model::Error> CreateShared(SharedParameters params,
                                                              uint32_t strength);

  /// @brief Initialize a subset engine over an already-shared parameter set.
  /// @see CreateShared, Create.
  static std::pair<CoverageEngine, model::Error> CreateShared(
      SharedParameters all_params, const std::vector<uint32_t>& param_subset, uint32_t strength);

  /// @brief The parameter set this engine reads.
  const std::vector<model::Parameter>& Parameters() const { return *params_; }

  /// @brief Mark all tuples covered by the given test case.
  void AddTestCase(const model::TestCase& test_case);

  /// @brief Clear all coverage and exclusion state, keeping the precomputed
  /// combination and lookup tables intact.
  ///
  /// Lets a single engine be reused across passes that share the same
  /// parameters and strength but apply different exclusions (e.g. negative-test
  /// generation, which fixes a different invalid value each pass), avoiding a
  /// full table rebuild per pass.
  void ResetCoverage() {
    covered_.Reset();
    invalid_tuples_ = 0;
    covered_bits_ = 0;
    scan_ci_ = 0;
    scan_vi_ = 0;
  }

  /// @brief Score a candidate value for a single parameter position.
  ///
  /// Answers a single "what would this one value gain?" query. Constructive
  /// greedy scores a whole parameter at once instead; see AddValueScores().
  uint32_t ScoreValue(const model::TestCase& partial, uint32_t param_index,
                      uint32_t value_index) const;

  /// @brief Score every value of one parameter against one partial assignment.
  ///
  /// Adds the coverage gain of assigning value vi to @p param_index into
  /// `out_scores[vi]`, for every value vi of that parameter. The caller owns the
  /// buffer, which must hold at least `Parameters()[param_index].size()` entries
  /// and be zeroed (or already hold another engine's contribution) on entry.
  ///
  /// Each relevant (combination, position) pair is visited once, and the shared
  /// part of the mixed-radix index is computed once per combination, so the
  /// extra work per candidate value is a single bit test. Scoring values one at
  /// a time repeats the whole sweep for every value.
  void AddValueScores(const model::TestCase& partial, uint32_t param_index,
                      uint32_t* out_scores) const;

  /// @brief Parameter combinations visited by AddValueScores() since construction.
  ///
  /// Diagnostic accessor. Each call adds its relevant-combination count once, so
  /// the counter costs nothing per value. Lets tests pin that scoring a
  /// parameter stays independent of how many values that parameter has.
  uint64_t ValueScoreComboVisits() const { return value_score_combo_visits_; }

  /// @brief Score a complete candidate test case.
  uint32_t ScoreCandidate(const model::TestCase& candidate) const;

  /// @brief Exclude tuples that are invalid due to constraints.
  ///
  /// For each t-tuple, builds a partial assignment and evaluates all
  /// constraints. If any constraint returns kFalse, the tuple is marked
  /// as covered (excluded) and does not count toward coverage goals.
  /// @param constraints Active constraints to evaluate.
  /// @param allowed_values Optional per-parameter allowed-value mask.
  /// @param budget_exceeded Optional out-flag set to true if any per-tuple
  ///   feasibility search exhausted its node budget; when set, exclusion stops
  ///   early so the caller can surface an explicit error rather than proceeding
  ///   on an incompletely classified tuple universe.
  void ExcludeInvalidTuples(const std::vector<model::Constraint>& constraints,
                            const std::vector<std::vector<bool>>& allowed_values = {},
                            bool* budget_exceeded = nullptr);

  /// @brief Exclude tuples containing any value disallowed by the mask.
  ///
  /// A mask that does not describe the model -- a different number of rows than
  /// there are parameters, or a row whose length differs from its parameter's
  /// domain -- allows nothing, so every tuple it fails to describe is excluded.
  /// @param allowed_values Per-parameter allowed-value mask.
  void ExcludeTuplesOutsideMask(const std::vector<std::vector<bool>>& allowed_values);

  /// Exclude tuples that do not contain the fixed parameter/value pair.
  void ExcludeTuplesNotContaining(uint32_t param_index, uint32_t value_index);

  /// @brief Exclude tuples that contain values marked as invalid in parameters.
  ///
  /// Any tuple containing at least one value where Parameter::is_invalid()
  /// returns true is marked as excluded. Used for positive-only generation.
  void ExcludeInvalidValues();

  /// @brief Return the total number of valid t-wise tuples.
  uint32_t TotalTuples() const { return total_tuples_ - invalid_tuples_; }

  /// @brief Return the number of covered valid tuples.
  ///
  /// O(1): the total number of set bits is tracked incrementally as tuples are
  /// covered or excluded, so this never rescans the bitset. `covered_bits_`
  /// counts every set bit (genuinely covered plus excluded), so subtracting the
  /// excluded count yields the covered valid tuples.
  uint32_t CoveredCount() const {
    assert(covered_bits_ >= invalid_tuples_);
    return covered_bits_ - invalid_tuples_;
  }

  /// @brief Return coverage ratio [0.0, 1.0].
  double CoverageRatio() const;

  /// @brief Check if all valid tuples are covered.
  bool IsComplete() const { return CoveredCount() == TotalTuples(); }

  /// @brief Collect uncovered tuples as human-readable objects.
  ///
  /// This is the diagnostic view, so it is bounded: @p limit is what a caller
  /// is willing to materialize, and model::kMaxDiagnosticTuples is what a result may
  /// carry. A caller that only needs a count or an overlap decision uses
  /// ForEachUncoveredTuple() instead, which builds nothing.
  /// @param params Parameter definitions (for resolving names and values).
  /// @return Vector of uncovered tuples with human-readable representations.
  std::vector<model::UncoveredTuple> GetUncoveredTuples(
      const std::vector<model::Parameter>& params,
      uint32_t limit = model::kMaxDiagnosticTuples) const;

  /// @brief Visit every currently-uncovered tuple as plain indices.
  ///
  /// Nothing is materialized per tuple: the callback borrows the walk's own
  /// buffers, so the cost of counting uncovered tuples does not depend on how
  /// many there are.
  /// @param fn Called as fn(const uint32_t* combo, const uint32_t* value_indices)
  ///           with `strength` entries in each, returning false to stop.
  template <typename Fn>
  void ForEachUncoveredTuple(Fn fn) const {
    ForEachTupleUntil([&fn](uint32_t /*global_index*/, const uint32_t* combo,
                            const std::vector<uint32_t>& value_indices) {
      return fn(combo, value_indices.data());
    });
  }

  /// @brief Whether this engine still counts the given tuple as uncovered.
  ///
  /// Tuple identity is the parameter combination plus its value tuple, so this
  /// answers for a tuple another engine enumerated. False when this engine does
  /// not enumerate that combination at all, and false when it holds it as
  /// covered or excluded.
  /// @param params Global parameter indices in ascending order.
  /// @param value_indices Value index per entry of @p params.
  /// @param count Size of both arrays; a tuple of another size is never one of
  ///        this engine's.
  bool NeedsTuple(const uint32_t* params, const uint32_t* value_indices, uint32_t count) const;

  /// @brief A single uncovered tuple expressed as a partial assignment.
  struct UncoveredAssignment {
    uint32_t index = 0;                ///< Global bit index of the tuple.
    std::vector<uint32_t> assignment;  ///< Global-sized partial; kUnassigned outside the tuple.
  };

  /// @brief Return the first currently-uncovered tuple, if any.
  ///
  /// The assignment fixes exactly this tuple's (parameter, value) pairs over the
  /// global parameter space, leaving all other positions kUnassigned. Used by the
  /// generator's completion phase to construct a test covering this tuple
  /// directly, rather than relying on randomized greedy construction.
  ///
  /// The scan resumes from an internal cursor instead of restarting at the first
  /// tuple. Coverage bits are only ever set until ResetCoverage() clears them and
  /// rewinds the cursor, so every index below the cursor is known to be covered
  /// or excluded and the returned tuple is still the lowest-indexed uncovered
  /// one. A whole pass of interleaved FirstUncovered() and AddTestCase() /
  /// ExcludeTuple() calls therefore stays linear in tuples plus calls.
  /// @return true if an uncovered tuple was found and written to @p out.
  bool FirstUncovered(UncoveredAssignment& out) const;

  /// @brief Coverage bits examined by FirstUncovered() since construction.
  ///
  /// Diagnostic accessor. A call tests exactly the bits it advances the cursor
  /// past plus the one it stops on, so the counter is updated once per call and
  /// adds nothing per bit. Lets tests bound the scan cost of a whole pass.
  uint64_t ScanBitTests() const { return scan_bit_tests_; }

  /// @brief Exclude a tuple (by global index) from the coverage target.
  ///
  /// Used when a tuple is partial-feasible but cannot be extended to any complete
  /// constraint-satisfying assignment, so it is genuinely unreachable and must
  /// not count as a coverage shortfall.
  void ExcludeTuple(uint32_t index);

 private:
  CoverageEngine() : params_(std::make_shared<const std::vector<model::Parameter>>()) {}

  SharedParameters params_;
  uint32_t strength_ = 0;
  uint32_t total_tuples_ = 0;
  uint32_t invalid_tuples_ = 0;
  /// @brief Number of set bits in covered_ (covered plus excluded tuples),
  /// maintained incrementally so CoveredCount() is O(1).
  uint32_t covered_bits_ = 0;
  util::DynamicBitset covered_;

  /// @brief Mapping from local param index to global param index.
  /// Empty means identity mapping (all params, no subset).
  std::vector<uint32_t> param_subset_;

  /// @brief Pre-computed C(n, t) parameter index combinations, stored flat with
  /// stride strength_ (combo ci occupies [ci*strength_, (ci+1)*strength_)). A
  /// flat buffer avoids ~one small heap block per combination near the cap.
  /// When param_subset_ is set, these contain GLOBAL param indices.
  std::vector<uint32_t> param_combinations_;
  uint32_t num_combinations_ = 0;
  std::vector<uint32_t> combination_offsets_;

  /// @brief param_to_combos_[p] = list of combination indices that include param p.
  std::vector<std::vector<uint32_t>> param_to_combos_;

  /// @brief param_position_in_combo_[p][k] = position of p within
  /// combination param_to_combos_[p][k].
  std::vector<std::vector<uint32_t>> param_position_in_combo_;

  /// @brief Mixed-radix multipliers per combination, stored flat with stride
  /// strength_: Mults(ci)[j] = product of value counts for positions j+1..t-1.
  /// Used for additive mixed-radix encoding instead of iterative multiply-accumulate.
  std::vector<uint32_t> combo_multipliers_;

  /// @brief Resume point of the FirstUncovered() scan: value tuple scan_vi_ of
  /// combination scan_ci_. Every tuple index below it is covered or excluded.
  mutable uint32_t scan_ci_ = 0;
  mutable uint32_t scan_vi_ = 0;

  /// @brief Bits examined by FirstUncovered(), see ScanBitTests().
  mutable uint64_t scan_bit_tests_ = 0;

  /// @brief Combinations visited by AddValueScores(), see ValueScoreComboVisits().
  mutable uint64_t value_score_combo_visits_ = 0;

  /// @brief Pointer to the stride-strength_ index block for combination ci.
  const uint32_t* Combo(uint32_t ci) const {
    return &param_combinations_[static_cast<size_t>(ci) * strength_];
  }

  /// @brief Pointer to the stride-strength_ multiplier block for combination ci.
  const uint32_t* Mults(uint32_t ci) const {
    return &combo_multipliers_[static_cast<size_t>(ci) * strength_];
  }

  /// @brief Flat tuple index the FirstUncovered() scan resumes at.
  uint32_t ScanPosition() const {
    return scan_ci_ < num_combinations_ ? combination_offsets_[scan_ci_] + scan_vi_ : total_tuples_;
  }

  void InitCombinations();
  void InitCombinationsFromSubset();
  void BuildLookupTables();
  uint32_t ComputeTotalTuples();

  /// @brief Iterate over all uncovered tuples, calling fn for each.
  ///
  /// Pre-allocates radixes and value_indices once per combination (not per tuple).
  /// @param fn Callback with signature:
  ///   fn(uint32_t global_index, const std::vector<uint32_t>& combo,
  ///      const std::vector<uint32_t>& value_indices)
  template <typename Fn>
  void ForEachTuple(Fn fn) const {
    std::vector<uint32_t> radixes(strength_);
    std::vector<uint32_t> value_indices(strength_);

    for (uint32_t ci = 0; ci < num_combinations_; ++ci) {
      const uint32_t* combo = Combo(ci);

      // Compute radixes once per combination.
      uint32_t product = 1;
      for (uint32_t j = 0; j < strength_; ++j) {
        radixes[j] = Parameters()[combo[j]].size();
        product *= radixes[j];
      }

      // Enumerate all value tuples.
      for (uint32_t vi = 0; vi < product; ++vi) {
        uint32_t global_index = combination_offsets_[ci] + vi;
        if (covered_.Test(global_index)) continue;

        util::DecodeMixedRadix(vi, radixes, value_indices);
        fn(global_index, combo, value_indices);
      }
    }
  }

  template <typename Fn>
  void ForEachTupleUntil(Fn fn) const {
    std::vector<uint32_t> radixes(strength_);
    std::vector<uint32_t> value_indices(strength_);
    for (uint32_t ci = 0; ci < num_combinations_; ++ci) {
      const uint32_t* combo = Combo(ci);
      uint32_t product = 1;
      for (uint32_t j = 0; j < strength_; ++j) {
        radixes[j] = Parameters()[combo[j]].size();
        product *= radixes[j];
      }
      for (uint32_t vi = 0; vi < product; ++vi) {
        uint32_t global_index = combination_offsets_[ci] + vi;
        if (covered_.Test(global_index)) continue;
        util::DecodeMixedRadix(vi, radixes, value_indices);
        if (!fn(global_index, combo, value_indices)) return;
      }
    }
  }
};

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_COVERAGE_ENGINE_H_
