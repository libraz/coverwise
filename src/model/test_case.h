/// @file test_case.h
/// @brief Test case and result representations.

#ifndef COVERWISE_MODEL_TEST_CASE_H_
#define COVERWISE_MODEL_TEST_CASE_H_

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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

/// @brief Suggested test case to add (for AI/human guidance).
struct Suggestion {
  std::string description;  ///< e.g. "Add test: os=win, browser=safari"
  TestCase test_case;
};

/// @brief Result of test generation.
struct GenerateResult {
  std::vector<TestCase> tests;
  double coverage = 0.0;
  std::vector<UncoveredTuple> uncovered;
  GenerateStats stats;
  std::vector<Suggestion> suggestions;
  std::vector<std::string> warnings;
};

/// @brief Structured error with context.
struct Error {
  enum class Code {
    kOk = 0,
    kConstraintError = 1,
    kInsufficientCoverage = 2,
    kInvalidInput = 3,
    kTupleExplosion = 4,
  };

  Code code = Code::kOk;
  std::string message;
  std::string detail;

  bool ok() const { return code == Code::kOk; }
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_TEST_CASE_H_
