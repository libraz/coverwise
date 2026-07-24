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

constexpr uint64_t kMaxTuples = 16'000'000;
constexpr uint64_t kMaxCombinations = 1'000'000;
constexpr size_t kMaxDiagnosticTuples = 1'000;

/// Recursion-node budget for a single feasibility search. Bounds the otherwise
/// exponential backtracking so a hard model terminates; an exhausted budget is
/// reported explicitly rather than being read as "infeasible".
constexpr uint64_t kMaxSearchNodes = 2'000'000;

struct SearchBudget {
  uint64_t remaining = kMaxSearchNodes;
  bool exceeded = false;
};

// Independent feasibility oracle. This intentionally does not use the core
// solver so validator agreement cannot be caused by shared implementation.
bool ValidatorSearch(const std::vector<model::Parameter>& params,
                     const std::vector<model::Constraint>& constraints,
                     std::vector<uint32_t>& assignment, uint32_t cursor, SearchBudget& budget) {
  if (budget.remaining == 0) {
    budget.exceeded = true;
    return false;
  }
  --budget.remaining;
  for (const auto& constraint : constraints) {
    if (constraint->Evaluate(assignment) == model::ConstraintResult::kFalse) {
      return false;
    }
  }
  while (cursor < params.size() && assignment[cursor] != model::kUnassigned) {
    ++cursor;
  }
  if (cursor == params.size()) {
    for (const auto& constraint : constraints) {
      if (constraint->Evaluate(assignment) != model::ConstraintResult::kTrue) {
        return false;
      }
    }
    return true;
  }
  for (uint32_t vi = 0; vi < params[cursor].size(); ++vi) {
    if (params[cursor].is_invalid(vi)) continue;
    assignment[cursor] = vi;
    if (ValidatorSearch(params, constraints, assignment, cursor + 1, budget)) return true;
    if (budget.exceeded) {
      assignment[cursor] = model::kUnassigned;
      return false;
    }
  }
  assignment[cursor] = model::kUnassigned;
  return false;
}

bool HasSatisfyingCompletion(const std::vector<model::Parameter>& params,
                             const std::vector<model::Constraint>& constraints,
                             const std::vector<uint32_t>& partial, bool* exceeded = nullptr) {
  auto assignment = partial;
  SearchBudget budget;
  bool result = ValidatorSearch(params, constraints, assignment, 0, budget);
  if (exceeded != nullptr) *exceeded = budget.exceeded;
  return result;
}

std::string ValidatePositiveTest(const model::TestCase& test,
                                 const std::vector<model::Parameter>& params,
                                 const std::vector<model::Constraint>& constraints) {
  if (test.values.size() != params.size()) {
    return "expected " + std::to_string(params.size()) + " value(s), got " +
           std::to_string(test.values.size());
  }
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    uint32_t vi = test.values[pi];
    if (vi == model::kUnassigned) {
      return "missing value for parameter " + params[pi].name;
    }
    if (vi >= params[pi].size()) {
      return "value index " + std::to_string(vi) + " is out of range for parameter " +
             params[pi].name;
    }
    if (params[pi].is_invalid(vi)) {
      return "value " + params[pi].name + "=" + params[pi].values[vi] + " is marked invalid";
    }
  }
  for (size_t ci = 0; ci < constraints.size(); ++ci) {
    if (constraints[ci]->Evaluate(test.values) != model::ConstraintResult::kTrue) {
      return "violates constraint #" + std::to_string(ci + 1) +
             " (constraint evaluation is false or indeterminate)";
    }
  }
  return {};
}

model::Error PreflightEnumeration(const std::vector<model::Parameter>& params, uint32_t strength) {
  const uint32_t n = static_cast<uint32_t>(params.size());
  if (strength == 0 || strength > n) return {};
  uint64_t combinations = 0;
  if (!util::CheckedBinomial(n, strength, kMaxCombinations, combinations)) {
    return {model::Error::Code::kTupleExplosion,
            "parameter combination metadata exceeds safety limit",
            "Combinations exceed limit: " + std::to_string(kMaxCombinations) +
                ". Reduce strength or parameter count."};
  }

  std::vector<uint32_t> combo(strength);
  for (uint32_t i = 0; i < strength; ++i) combo[i] = i;
  uint64_t total = 0;
  for (;;) {
    uint64_t product = 1;
    for (uint32_t pi : combo) {
      uint64_t radix = params[pi].size();
      if (radix != 0 && product > kMaxTuples / radix) {
        return {model::Error::Code::kTupleExplosion, "t-wise tuple count exceeds safety limit",
                "Total tuples exceed limit: " + std::to_string(kMaxTuples)};
      }
      product *= radix;
    }
    if (total > kMaxTuples - product) {
      return {model::Error::Code::kTupleExplosion, "t-wise tuple count exceeds safety limit",
              "Total tuples exceed limit: " + std::to_string(kMaxTuples)};
    }
    total += product;

    int pos = static_cast<int>(strength) - 1;
    while (pos >= 0 && combo[pos] == n - strength + static_cast<uint32_t>(pos)) --pos;
    if (pos < 0) break;
    ++combo[pos];
    for (uint32_t i = static_cast<uint32_t>(pos) + 1; i < strength; ++i) {
      combo[i] = combo[i - 1] + 1;
    }
  }
  return {};
}

}  // namespace

CoverageReport ValidateCoverage(const std::vector<model::Parameter>& params,
                                const std::vector<model::TestCase>& tests, uint32_t strength,
                                const std::vector<model::Constraint>& constraints) {
  CoverageReport report;
  uint32_t n = static_cast<uint32_t>(params.size());

  // A strength of 0, or greater than the parameter count, is invalid input —
  // the same rule generate enforces (options_validation.cpp). Reporting vacuous
  // 100% coverage here would make the oracle green-light an unanswerable query.
  if (strength == 0 || strength > n) {
    report.error = {model::Error::Code::kInvalidInput,
                    "Strength must be between 1 and parameter count",
                    "strength=" + std::to_string(strength) + ", parameters=" + std::to_string(n)};
    return report;
  }

  report.error = PreflightEnumeration(params, strength);
  if (!report.error.ok()) return report;

  // Step 1: Generate all C(n, strength) combinations of parameter indices.
  auto combinations = util::GenerateCombinations(n, strength);

  // Reusable assignment buffer for constraint evaluation.
  std::vector<uint32_t> assignment(n, model::kUnassigned);
  std::vector<const model::TestCase*> valid_tests;
  valid_tests.reserve(tests.size());
  for (uint32_t i = 0; i < tests.size(); ++i) {
    auto reason = ValidatePositiveTest(tests[i], params, constraints);
    if (reason.empty()) {
      valid_tests.push_back(&tests[i]);
    } else {
      report.invalid_tests.push_back({i, std::move(reason)});
    }
  }

  // Buffers reused across combinations to avoid per-tuple heap allocation.
  std::vector<uint32_t> value_indices(strength);
  std::vector<char> covered_flags;

  for (const auto& combo : combinations) {
    // Step 2: Enumerate all value tuples (cartesian product) for this combination.
    // Compute the number of tuples for this combination. Accumulate in 64-bit to
    // avoid silently wrapping (and under-counting) for large value counts.
    uint64_t num_tuples = 1;
    for (uint32_t pi : combo) {
      num_tuples *= params[pi].size();
    }

    // Step 2.5: Project every valid test onto its flat value-tuple index for this
    // combination in a single pass, so the coverage check below is an O(1) lookup
    // instead of rescanning all tests per tuple. A valid test always has an
    // in-range value for every parameter (guaranteed by ValidatePositiveTest), so
    // the projection is total. The encoding here (most-significant digit first)
    // must match the decode in the tuple loop.
    covered_flags.assign(static_cast<size_t>(num_tuples), 0);
    for (const auto* test : valid_tests) {
      uint64_t flat = 0;
      for (uint32_t j = 0; j < strength; ++j) {
        flat = flat * params[combo[j]].size() + test->values[combo[j]];
      }
      covered_flags[static_cast<size_t>(flat)] = 1;
    }

    // Iterate over all value tuples using a flat index.
    for (uint64_t flat = 0; flat < num_tuples; ++flat) {
      // Decode flat index into value indices (mixed-radix decomposition).
      uint64_t remainder = flat;
      for (int i = static_cast<int>(strength) - 1; i >= 0; --i) {
        uint32_t radix = params[combo[i]].size();
        value_indices[i] = static_cast<uint32_t>(remainder % radix);
        remainder /= radix;
      }

      // Step 2a: Exclude tuples containing any invalid value from the coverage
      // universe entirely (matches the generator's
      // CoverageEngine::ExcludeInvalidValues semantics). Such tuples do not
      // count toward total_tuples or the uncovered list.
      bool contains_invalid_value = false;
      for (uint32_t j = 0; j < strength; ++j) {
        if (params[combo[j]].is_invalid(value_indices[j])) {
          contains_invalid_value = true;
          break;
        }
      }
      if (contains_invalid_value) {
        continue;
      }

      // Step 2b: If any constraint marks this partial assignment as kFalse,
      // exclude this tuple from the coverage universe entirely (matches the
      // generator's CoverageEngine::ExcludeInvalidTuples semantics).
      if (!constraints.empty()) {
        for (uint32_t j = 0; j < strength; ++j) {
          assignment[combo[j]] = value_indices[j];
        }
        bool tuple_exceeded = false;
        bool excluded = !HasSatisfyingCompletion(params, constraints, assignment, &tuple_exceeded);
        // Reset assignment for reuse.
        for (uint32_t j = 0; j < strength; ++j) {
          assignment[combo[j]] = model::kUnassigned;
        }
        if (tuple_exceeded) {
          report.error = {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
                          "Tuple feasibility could not be determined within the search budget"};
          return report;
        }
        if (excluded) {
          continue;
        }
      }

      ++report.total_tuples;

      // Step 3: Coverage is an O(1) lookup into the projection built above.
      if (covered_flags[static_cast<size_t>(flat)]) {
        ++report.covered_tuples;
      } else {
        ++report.uncovered_count;
        if (report.uncovered.size() >= kMaxDiagnosticTuples) continue;
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
  report.omitted_uncovered = report.uncovered_count - report.uncovered.size();

  // Compute coverage ratio. When there are no tuples, coverage is vacuously 1.0.
  if (report.total_tuples == 0) {
    report.coverage_ratio = 1.0;
  } else {
    report.coverage_ratio =
        static_cast<double>(report.covered_tuples) / static_cast<double>(report.total_tuples);
  }

  return report;
}

namespace {

/// @brief Check whether a class tuple has at least one valid, constraint-satisfiable
///        representative value assignment.
///
/// A class tuple selects one equivalence class per parameter in @p combo. A
/// representative picks, for each such parameter, a concrete value whose class
/// matches the required class. The tuple is "satisfiable" if some choice of
/// representatives uses no invalid value and violates no constraint. Class
/// tuples with no satisfiable representative are excluded from the coverage
/// universe (mirroring the value-level invalid/constraint exclusion).
bool ClassTupleHasValidRepresentative(const std::vector<model::Parameter>& params,
                                      const std::vector<uint32_t>& class_param_indices,
                                      const std::vector<std::string>& required_classes,
                                      const std::vector<model::Constraint>& constraints) {
  uint32_t k = static_cast<uint32_t>(class_param_indices.size());

  // For each parameter, collect the value indices whose class matches and which
  // are not invalid. If any parameter has no such value, no representative exists.
  std::vector<std::vector<uint32_t>> candidates(k);
  for (uint32_t i = 0; i < k; ++i) {
    uint32_t pi = class_param_indices[i];
    const auto& p = params[pi];
    for (uint32_t v = 0; v < p.size(); ++v) {
      if (p.is_invalid(v)) continue;
      if (p.equivalence_class(v) == required_classes[i]) {
        candidates[i].push_back(v);
      }
    }
    if (candidates[i].empty()) return false;
  }

  if (constraints.empty()) {
    return true;
  }

  // Search the cartesian product of candidate values for a constraint-satisfying
  // assignment. The candidate space is bounded by the values-per-class.
  std::vector<uint32_t> assignment(params.size(), model::kUnassigned);
  std::vector<uint32_t> choice(k, 0);
  for (;;) {
    for (uint32_t i = 0; i < k; ++i) {
      assignment[class_param_indices[i]] = candidates[i][choice[i]];
    }
    bool violated = !HasSatisfyingCompletion(params, constraints, assignment);
    for (uint32_t i = 0; i < k; ++i) {
      assignment[class_param_indices[i]] = model::kUnassigned;
    }
    if (!violated) return true;

    // Advance the mixed-radix choice vector.
    int pos = static_cast<int>(k) - 1;
    while (pos >= 0) {
      if (++choice[pos] < candidates[pos].size()) break;
      choice[pos] = 0;
      --pos;
    }
    if (pos < 0) break;
  }
  return false;
}

}  // namespace

ClassCoverageReport ComputeClassCoverage(const std::vector<model::Parameter>& params,
                                         const std::vector<model::TestCase>& tests,
                                         uint32_t strength,
                                         const std::vector<model::Constraint>& constraints) {
  ClassCoverageReport report;
  uint32_t n = static_cast<uint32_t>(params.size());

  // A universe with no class tuples to cover is vacuously fully covered (1.0),
  // matching the total_class_tuples == 0 branch below and ValidateCoverage's
  // empty-universe handling. Only a genuine enumeration error keeps ratio 0.0.
  if (strength == 0 || strength > n) {
    report.coverage_ratio = 1.0;
    return report;
  }

  report.error = PreflightEnumeration(params, strength);
  if (!report.error.ok()) return report;

  // Identify parameters that have equivalence classes.
  std::vector<uint32_t> class_params;
  for (uint32_t i = 0; i < n; ++i) {
    if (params[i].has_equivalence_classes()) {
      class_params.push_back(i);
    }
  }

  if (class_params.empty()) {
    report.coverage_ratio = 1.0;
    return report;
  }

  // For class coverage we consider combinations of parameters that have classes.
  // If fewer parameters have classes than the strength, use the available count.
  uint32_t class_n = static_cast<uint32_t>(class_params.size());
  uint32_t effective_strength = std::min(strength, class_n);

  // Generate all C(class_n, effective_strength) combinations of class-enabled parameters.
  auto combinations = util::GenerateCombinations(class_n, effective_strength);
  std::vector<const model::TestCase*> valid_tests;
  valid_tests.reserve(tests.size());
  for (const auto& test : tests) {
    if (ValidatePositiveTest(test, params, constraints).empty()) valid_tests.push_back(&test);
  }

  // For each combination, enumerate all class tuples (cartesian product of unique classes).
  // Use a set of string tuples to track covered class combinations.
  for (const auto& combo : combinations) {
    // Get the unique classes for each parameter in this combination.
    std::vector<std::vector<std::string>> classes_per_param;
    for (uint32_t idx : combo) {
      classes_per_param.push_back(params[class_params[idx]].unique_classes());
    }

    // Resolve the global parameter indices for this class combination once.
    std::vector<uint32_t> combo_param_indices(effective_strength);
    for (uint32_t k = 0; k < effective_strength; ++k) {
      combo_param_indices[k] = class_params[combo[k]];
    }

    // Compute the number of class tuples for this combination. Accumulate in
    // 64-bit so a large class product cannot silently wrap.
    uint64_t num_tuples = 1;
    for (const auto& cls : classes_per_param) {
      num_tuples *= cls.size();
    }

    // Enumerate all class tuples and check coverage.
    for (uint64_t flat = 0; flat < num_tuples; ++flat) {
      // Decode flat index into class indices.
      std::vector<uint32_t> class_indices(effective_strength);
      uint64_t remainder = flat;
      for (int i = static_cast<int>(effective_strength) - 1; i >= 0; --i) {
        uint32_t radix = static_cast<uint32_t>(classes_per_param[i].size());
        class_indices[i] = static_cast<uint32_t>(remainder % radix);
        remainder /= radix;
      }

      // Exclude class tuples with no valid, constraint-satisfiable representative
      // from the universe entirely (they do not count toward total_class_tuples).
      std::vector<std::string> required_classes(effective_strength);
      for (uint32_t k = 0; k < effective_strength; ++k) {
        required_classes[k] = classes_per_param[k][class_indices[k]];
      }
      if (!ClassTupleHasValidRepresentative(params, combo_param_indices, required_classes,
                                            constraints)) {
        continue;
      }

      ++report.total_class_tuples;

      // Check if any test case covers this class tuple.
      bool covered = false;
      for (const auto* test : valid_tests) {
        bool matches = true;
        for (uint32_t k = 0; k < effective_strength; ++k) {
          uint32_t pi = class_params[combo[k]];
          uint32_t vi = test->values[pi];
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
  } else {
    report.coverage_ratio = 1.0;
  }

  return report;
}

void AnnotateClassCoverage(model::GenerateResult& result,
                           const std::vector<model::Parameter>& params, uint32_t strength,
                           const std::vector<model::Constraint>& constraints) {
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

  auto class_report = ComputeClassCoverage(params, result.tests, strength, constraints);
  result.class_coverage = model::ClassCoverage{
      class_report.total_class_tuples,
      class_report.covered_class_tuples,
      class_report.coverage_ratio,
  };
}

}  // namespace validator
}  // namespace coverwise
