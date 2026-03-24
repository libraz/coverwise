/// @file coverage_validator.cpp

#include "validator/coverage_validator.h"

#include <algorithm>
#include <string>

namespace coverwise {
namespace validator {

namespace {

/// @brief Generate all C(n, k) combinations of indices [0, n) iteratively.
///
/// Each combination is a sorted vector of k indices.
std::vector<std::vector<uint32_t>> GenerateCombinations(uint32_t n, uint32_t k) {
  std::vector<std::vector<uint32_t>> result;
  if (k == 0 || k > n) {
    return result;
  }

  // Initialize first combination: [0, 1, ..., k-1]
  std::vector<uint32_t> combo(k);
  for (uint32_t i = 0; i < k; ++i) {
    combo[i] = i;
  }

  while (true) {
    result.push_back(combo);

    // Find the rightmost element that can be incremented.
    int i = static_cast<int>(k) - 1;
    while (i >= 0 && combo[i] == n - k + static_cast<uint32_t>(i)) {
      --i;
    }
    if (i < 0) {
      break;
    }

    // Increment it and reset all elements to the right.
    ++combo[i];
    for (uint32_t j = static_cast<uint32_t>(i) + 1; j < k; ++j) {
      combo[j] = combo[j - 1] + 1;
    }
  }

  return result;
}

/// @brief Check if a test case covers a specific value tuple for given parameter indices.
///
/// @param test The test case to check.
/// @param param_indices The parameter indices in the tuple.
/// @param value_indices The expected value indices for each parameter.
/// @return true if the test case matches all values in the tuple.
bool TestCovers(const model::TestCase& test,
                const std::vector<uint32_t>& param_indices,
                const std::vector<uint32_t>& value_indices) {
  for (size_t i = 0; i < param_indices.size(); ++i) {
    uint32_t pi = param_indices[i];
    if (pi >= test.values.size() || test.values[pi] != value_indices[i]) {
      return false;
    }
  }
  return true;
}

}  // namespace

CoverageReport ValidateCoverage(const std::vector<model::Parameter>& params,
                                const std::vector<model::TestCase>& tests,
                                uint32_t strength) {
  CoverageReport report;
  uint32_t n = static_cast<uint32_t>(params.size());

  // Edge case: strength is 0 or exceeds parameter count.
  if (strength == 0 || strength > n) {
    return report;
  }

  // Step 1: Generate all C(n, strength) combinations of parameter indices.
  auto combinations = GenerateCombinations(n, strength);

  for (const auto& combo : combinations) {
    // Step 2: Enumerate all value tuples (cartesian product) for this combination.
    // Compute the number of tuples for this combination.
    uint32_t num_tuples = 1;
    for (uint32_t pi : combo) {
      num_tuples *= params[pi].size();
    }

    // Iterate over all value tuples using a flat index.
    for (uint32_t flat = 0; flat < num_tuples; ++flat) {
      // Decode flat index into value indices (mixed-radix decomposition).
      std::vector<uint32_t> value_indices(strength);
      uint32_t remainder = flat;
      for (int i = static_cast<int>(strength) - 1; i >= 0; --i) {
        uint32_t radix = params[combo[i]].size();
        value_indices[i] = remainder % radix;
        remainder /= radix;
      }

      ++report.total_tuples;

      // Step 3: Check if any test case covers this value tuple.
      bool covered = false;
      for (const auto& test : tests) {
        if (TestCovers(test, combo, value_indices)) {
          covered = true;
          break;
        }
      }

      if (covered) {
        ++report.covered_tuples;
      } else {
        // Build the UncoveredTuple with human-readable strings.
        model::UncoveredTuple uncovered;
        uncovered.reason = "never covered";
        for (size_t i = 0; i < strength; ++i) {
          uint32_t pi = combo[i];
          uint32_t vi = value_indices[i];
          uncovered.params.push_back(params[pi].name);
          uncovered.tuple.push_back(params[pi].name + "=" + params[pi].values[vi]);
        }
        report.uncovered.push_back(std::move(uncovered));
      }
    }
  }

  // Compute coverage ratio.
  if (report.total_tuples > 0) {
    report.coverage_ratio =
        static_cast<double>(report.covered_tuples) / static_cast<double>(report.total_tuples);
  }

  return report;
}

}  // namespace validator
}  // namespace coverwise
