/// @file coverage_engine.h
/// @brief Coverage tracking engine for t-wise tuple coverage.

#ifndef COVERWISE_CORE_COVERAGE_ENGINE_H_
#define COVERWISE_CORE_COVERAGE_ENGINE_H_

#include <cstdint>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "util/bitset.h"

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
  void ExcludeInvalidTuples(const std::vector<model::Constraint>& constraints);

  /// @brief Exclude tuples that contain values marked as invalid in parameters.
  ///
  /// Any tuple containing at least one value where Parameter::is_invalid()
  /// returns true is marked as excluded. Used for positive-only generation.
  void ExcludeInvalidValues();

  /// @brief Return the total number of valid t-wise tuples.
  uint32_t TotalTuples() const { return total_tuples_ - invalid_tuples_; }

  /// @brief Return the number of covered valid tuples.
  uint32_t CoveredCount() const { return covered_.Count() - invalid_tuples_; }

  /// @brief Return coverage ratio [0.0, 1.0].
  double CoverageRatio() const;

  /// @brief Check if all valid tuples are covered.
  bool IsComplete() const { return CoveredCount() == TotalTuples(); }

  /// @brief Collect all uncovered tuples as human-readable objects.
  /// @param params Parameter definitions (for resolving names and values).
  /// @return Vector of uncovered tuples with human-readable representations.
  std::vector<model::UncoveredTuple> GetUncoveredTuples(
      const std::vector<model::Parameter>& params) const;

 private:
  CoverageEngine() = default;

  std::vector<model::Parameter> params_;
  uint32_t strength_ = 0;
  uint32_t total_tuples_ = 0;
  uint32_t invalid_tuples_ = 0;
  util::DynamicBitset covered_;

  /// @brief Mapping from local param index to global param index.
  /// Empty means identity mapping (all params, no subset).
  std::vector<uint32_t> param_subset_;

  /// @brief Pre-computed C(n, t) parameter index combinations.
  /// When param_subset_ is set, these contain GLOBAL param indices.
  std::vector<std::vector<uint32_t>> param_combinations_;
  std::vector<uint32_t> combination_offsets_;

  /// @brief param_to_combos_[p] = list of combination indices that include param p.
  std::vector<std::vector<uint32_t>> param_to_combos_;

  /// @brief param_position_in_combo_[p][k] = position of p within param_combinations_[param_to_combos_[p][k]].
  std::vector<std::vector<uint32_t>> param_position_in_combo_;

  /// @brief combo_multipliers_[ci][j] = product of value counts for positions j+1..t-1.
  /// Used for additive mixed-radix encoding instead of iterative multiply-accumulate.
  std::vector<std::vector<uint32_t>> combo_multipliers_;

  void InitCombinations();
  void InitCombinationsFromSubset();
  void BuildLookupTables();
  uint32_t ComputeTotalTuples();
};

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_COVERAGE_ENGINE_H_
