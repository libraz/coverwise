/// @file coverage_engine.h
/// @brief Coverage tracking engine for t-wise tuple coverage.

#ifndef COVERWISE_CORE_COVERAGE_ENGINE_H_
#define COVERWISE_CORE_COVERAGE_ENGINE_H_

#include <cassert>
#include <cstdint>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"
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
  /// @brief Maximum number of tuples before refusing to proceed.
  /// ~16M tuples ≈ 2MB bitset. Beyond this, performance degrades.
  static constexpr uint32_t kMaxTuples = 16'000'000;
  /// Maximum materialized parameter combinations and lookup-table rows.
  static constexpr uint32_t kMaxCombinations = 1'000'000;
  /// Maximum number of human-readable uncovered tuples returned per result.
  static constexpr uint32_t kMaxDiagnosticTuples = 1'000;

  /// @brief Initialize coverage tracking for the given parameters and strength.
  /// @param params The parameter definitions.
  /// @param strength The interaction strength (t). 2 = pairwise.
  /// @return Error if tuple count exceeds kMaxTuples.
  static std::pair<CoverageEngine, model::Error> Create(const std::vector<model::Parameter>& params,
                                                        uint32_t strength);

  /// @brief Initialize coverage tracking for a subset of parameters.
  ///
  /// Only the parameters at the given indices are considered for tuple
  /// generation. Test cases still use global parameter indices.
  /// @param all_params All parameter definitions.
  /// @param param_subset Indices of parameters to cover (must be sorted).
  /// @param strength The interaction strength (t).
  /// @return Error if tuple count exceeds kMaxTuples.
  static std::pair<CoverageEngine, model::Error> Create(
      const std::vector<model::Parameter>& all_params, const std::vector<uint32_t>& param_subset,
      uint32_t strength);

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
  }

  /// @brief Score a candidate value for a single parameter position.
  ///
  /// Used by constructive greedy: given a partial assignment, how many new
  /// tuples would be covered by setting param_index to value_index?
  uint32_t ScoreValue(const model::TestCase& partial, uint32_t param_index,
                      uint32_t value_index) const;

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

  /// Exclude tuples containing any value disallowed by the mask.
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

  /// @brief Collect all uncovered tuples as human-readable objects.
  /// @param params Parameter definitions (for resolving names and values).
  /// @return Vector of uncovered tuples with human-readable representations.
  std::vector<model::UncoveredTuple> GetUncoveredTuples(
      const std::vector<model::Parameter>& params, uint32_t limit = kMaxDiagnosticTuples) const;

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
  /// @return true if an uncovered tuple was found and written to @p out.
  bool FirstUncovered(UncoveredAssignment& out) const;

  /// @brief Exclude a tuple (by global index) from the coverage target.
  ///
  /// Used when a tuple is partial-feasible but cannot be extended to any complete
  /// constraint-satisfying assignment, so it is genuinely unreachable and must
  /// not count as a coverage shortfall.
  void ExcludeTuple(uint32_t index);

 private:
  CoverageEngine() = default;

  std::vector<model::Parameter> params_;
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

  /// @brief Pointer to the stride-strength_ index block for combination ci.
  const uint32_t* Combo(uint32_t ci) const {
    return &param_combinations_[static_cast<size_t>(ci) * strength_];
  }

  /// @brief Pointer to the stride-strength_ multiplier block for combination ci.
  const uint32_t* Mults(uint32_t ci) const {
    return &combo_multipliers_[static_cast<size_t>(ci) * strength_];
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
        radixes[j] = params_[combo[j]].size();
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
        radixes[j] = params_[combo[j]].size();
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
