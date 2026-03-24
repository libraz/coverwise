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

  uint32_t TupleIndex(const std::vector<uint32_t>& param_indices,
                      const std::vector<uint32_t>& value_indices) const;

  /// @brief Pre-computed C(n, t) parameter index combinations.
  std::vector<std::vector<uint32_t>> param_combinations_;
  std::vector<uint32_t> combination_offsets_;

  void InitCombinations();
  uint32_t ComputeTotalTuples() const;
};

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_COVERAGE_ENGINE_H_
