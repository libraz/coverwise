/// @file test_case.h
/// @brief Test case and result representations.

#ifndef COVERWISE_MODEL_TEST_CASE_H_
#define COVERWISE_MODEL_TEST_CASE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/error.h"
#include "model/parameter.h"

namespace coverwise {
namespace model {

/// @brief A single test case: a vector of value indices, one per parameter.
///
/// values[i] is the index into Parameter[i].values.
struct TestCase {
  std::vector<uint32_t> values;
};

/// @brief A human-readable representation of an uncovered tuple.
struct UncoveredTuple {
  /// @brief e.g. ["os=win", "browser=safari"]
  std::vector<std::string> tuple;
  /// @brief Parameter names involved, e.g. ["os", "browser"]
  std::vector<std::string> params;
  /// @brief (parameter index, value index) pairs identifying this tuple.
  ///
  /// Carried alongside the string form so callers can reconstruct a witness
  /// assignment without parsing "name=value" strings, which is ambiguous when a
  /// parameter name or value contains '='.
  std::vector<std::pair<uint32_t, uint32_t>> indices;
  /// @brief Why this tuple is uncovered.
  std::string reason = "never covered";

  /// @brief Format as readable string: "os=win, browser=safari"
  std::string ToString() const;
};

/// @brief Generation statistics for evaluation and comparison.
struct GenerateStats {
  uint64_t total_tuples = 0;
  uint64_t covered_tuples = 0;
  uint32_t test_count = 0;
};

/// @brief Coverage metrics for single-fault negative tests.
///
/// Each feasible requested-strength tuple containing exactly one invalid value
/// is counted once. `omitted_tuples` is the number left uncovered, for example
/// because `maxTests` capped the combined positive and negative suite.
struct NegativeCoverage {
  uint64_t total_tuples = 0;
  uint64_t covered_tuples = 0;
  uint64_t omitted_tuples = 0;
  double coverage_ratio = 1.0;
};

/// @brief Equivalence class coverage metrics.
struct ClassCoverage {
  uint64_t total_class_tuples = 0;
  uint64_t covered_class_tuples = 0;
  double class_coverage_ratio = 0.0;
};

/// @brief Suggested test case to add (for AI/human guidance).
struct Suggestion {
  std::string description;  ///< e.g. "Add test: os=win, browser=safari"
  TestCase test_case;
};

/// @brief Result of test generation.
///
/// Note on sub-model metrics: When sub-models are used, `coverage` reports
/// the minimum coverage ratio across all engines (global + sub-models),
/// while `stats.total_tuples` and `stats.covered_tuples` are the sum across
/// all engines. Thus `stats.covered_tuples / stats.total_tuples` may differ
/// from `coverage`. Use `coverage` for pass/fail decisions; use `stats` for
/// understanding total workload.
struct GenerateResult {
  /// @brief Effective parameter value space used by generation.
  ///
  /// Boundary expansion can reorder and add values, so callers must render
  /// TestCase indices against this vector rather than the input options.
  std::vector<Parameter> parameters;
  std::vector<TestCase> tests;           ///< Positive tests (no invalid values)
  std::vector<TestCase> negative_tests;  ///< Negative tests (exactly 1 invalid value each)
  std::optional<NegativeCoverage> negative_coverage;  ///< Present when invalid values exist
  double coverage = 0.0;                              ///< Minimum coverage ratio across all engines
  std::vector<UncoveredTuple> uncovered;
  uint64_t uncovered_count = 0;  ///< Total uncovered tuples before diagnostic truncation.
  uint64_t omitted_uncovered = 0;
  GenerateStats stats;
  std::vector<Suggestion> suggestions;
  std::vector<std::string> warnings;
  std::optional<ClassCoverage> class_coverage;  ///< Equivalence class coverage (if classes defined)
  /// @brief Machine-readable error signal for early-exit conditions.
  ///
  /// Set to a non-ok code when generation aborts before producing a suite,
  /// e.g. a constraint parse error (kConstraintError) or invalid input
  /// (kInvalidInput). Remains kOk when generation runs to completion, even if
  /// coverage is incomplete (insufficient coverage is reported via `coverage`).
  Error error;
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_TEST_CASE_H_
