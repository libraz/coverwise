/// @file coverage_validator.cpp

#include "validator/coverage_validator.h"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <vector>

#include "util/combinatorics.h"

namespace coverwise {
namespace validator {

namespace {

/// @brief Check if a test case covers a specific value tuple for given parameter indices.
///
/// @param test The test case to check.
/// @param param_indices The parameter indices in the tuple.
/// @param value_indices The expected value indices for each parameter.
/// @return true if the test case matches all values in the tuple.
bool TestCovers(const model::TestCase& test, const std::vector<uint32_t>& param_indices,
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
                                const std::vector<model::TestCase>& tests, uint32_t strength) {
  CoverageReport report;
  uint32_t n = static_cast<uint32_t>(params.size());

  // Edge case: strength is 0 or exceeds parameter count.
  // Vacuous coverage: nothing to cover means everything is covered.
  if (strength == 0 || strength > n) {
    report.coverage_ratio = 1.0;
    return report;
  }

  // Step 1: Generate all C(n, strength) combinations of parameter indices.
  auto combinations = util::GenerateCombinations(n, strength);

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

  // Compute coverage ratio. When there are no tuples, coverage is vacuously 1.0.
  if (report.total_tuples == 0) {
    report.coverage_ratio = 1.0;
  } else {
    report.coverage_ratio =
        static_cast<double>(report.covered_tuples) / static_cast<double>(report.total_tuples);
  }

  return report;
}

ClassCoverageReport ComputeClassCoverage(const std::vector<model::Parameter>& params,
                                         const std::vector<model::TestCase>& tests,
                                         uint32_t strength) {
  ClassCoverageReport report;
  uint32_t n = static_cast<uint32_t>(params.size());

  if (strength == 0 || strength > n) {
    return report;
  }

  // Identify parameters that have equivalence classes.
  std::vector<uint32_t> class_params;
  for (uint32_t i = 0; i < n; ++i) {
    if (params[i].has_equivalence_classes()) {
      class_params.push_back(i);
    }
  }

  if (class_params.empty()) {
    return report;
  }

  // For class coverage we consider combinations of parameters that have classes.
  // If fewer parameters have classes than the strength, use the available count.
  uint32_t class_n = static_cast<uint32_t>(class_params.size());
  uint32_t effective_strength = std::min(strength, class_n);

  // Generate all C(class_n, effective_strength) combinations of class-enabled parameters.
  auto combinations = util::GenerateCombinations(class_n, effective_strength);

  // For each combination, enumerate all class tuples (cartesian product of unique classes).
  // Use a set of string tuples to track covered class combinations.
  for (const auto& combo : combinations) {
    // Get the unique classes for each parameter in this combination.
    std::vector<std::vector<std::string>> classes_per_param;
    for (uint32_t idx : combo) {
      classes_per_param.push_back(params[class_params[idx]].unique_classes());
    }

    // Compute the number of class tuples for this combination.
    uint32_t num_tuples = 1;
    for (const auto& cls : classes_per_param) {
      num_tuples *= static_cast<uint32_t>(cls.size());
    }

    // Enumerate all class tuples and check coverage.
    for (uint32_t flat = 0; flat < num_tuples; ++flat) {
      // Decode flat index into class indices.
      std::vector<uint32_t> class_indices(effective_strength);
      uint32_t remainder = flat;
      for (int i = static_cast<int>(effective_strength) - 1; i >= 0; --i) {
        uint32_t radix = static_cast<uint32_t>(classes_per_param[i].size());
        class_indices[i] = remainder % radix;
        remainder /= radix;
      }

      ++report.total_class_tuples;

      // Check if any test case covers this class tuple.
      bool covered = false;
      for (const auto& test : tests) {
        bool matches = true;
        for (uint32_t k = 0; k < effective_strength; ++k) {
          uint32_t pi = class_params[combo[k]];
          if (pi >= static_cast<uint32_t>(test.values.size())) {
            matches = false;
            break;
          }
          uint32_t vi = test.values[pi];
          const std::string& test_class = params[pi].equivalence_class(vi);
          if (test_class != classes_per_param[k][class_indices[k]]) {
            matches = false;
            break;
          }
        }
        if (matches) {
          covered = true;
          break;
        }
      }

      if (covered) {
        ++report.covered_class_tuples;
      }
    }
  }

  if (report.total_class_tuples > 0) {
    report.coverage_ratio = static_cast<double>(report.covered_class_tuples) /
                            static_cast<double>(report.total_class_tuples);
  }

  return report;
}

void AnnotateClassCoverage(model::GenerateResult& result,
                           const std::vector<model::Parameter>& params, uint32_t strength) {
  bool has_eq_classes = false;
  for (const auto& p : params) {
    if (p.has_equivalence_classes()) {
      has_eq_classes = true;
      break;
    }
  }
  if (!has_eq_classes) {
    return;
  }

  auto class_report = ComputeClassCoverage(params, result.tests, strength);
  result.class_coverage = model::ClassCoverage{
      class_report.total_class_tuples,
      class_report.covered_class_tuples,
      class_report.coverage_ratio,
  };
}

}  // namespace validator
}  // namespace coverwise
