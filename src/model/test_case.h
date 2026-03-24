/// @file test_case.h
/// @brief Test case and result representations.

#ifndef COVERWISE_MODEL_TEST_CASE_H_
#define COVERWISE_MODEL_TEST_CASE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "model/error.h"

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
  /// @brief Why this tuple is uncovered.
  std::string reason = "never covered";

  /// @brief Format as readable string: "os=win, browser=safari"
  std::string ToString() const;
};

/// @brief Generation statistics for evaluation and comparison.
struct GenerateStats {
  uint32_t total_tuples = 0;
  uint32_t covered_tuples = 0;
  uint32_t test_count = 0;
};

/// @brief Equivalence class coverage metrics.
struct ClassCoverage {
  uint32_t total_class_tuples = 0;
  uint32_t covered_class_tuples = 0;
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
  std::vector<TestCase> tests;           ///< Positive tests (no invalid values)
  std::vector<TestCase> negative_tests;  ///< Negative tests (exactly 1 invalid value each)
  double coverage = 0.0;                 ///< Minimum coverage ratio across all engines
  std::vector<UncoveredTuple> uncovered;
  GenerateStats stats;
  std::vector<Suggestion> suggestions;
  std::vector<std::string> warnings;
  ClassCoverage class_coverage;         ///< Equivalence class coverage (if classes defined)
  bool has_class_coverage = false;      ///< True if any parameter has equivalence classes
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_TEST_CASE_H_
