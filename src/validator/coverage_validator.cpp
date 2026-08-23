/// @file coverage_validator.cpp

#include "validator/coverage_validator.h"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "util/combinatorics.h"

namespace coverwise {
namespace validator {

namespace {

constexpr uint64_t kMaxTuples = 16'000'000;
constexpr uint32_t kMaxCombinations = 1'000'000;
constexpr size_t kMaxDiagnosticTuples = 1'000;

/// Recursion-node budget for a single feasibility search. Bounds the otherwise
/// exponential backtracking so a hard model terminates; an exhausted budget is
/// reported explicitly rather than being read as "infeasible".
constexpr uint64_t kMaxSearchNodes = 2'000'000;

struct SearchBudget {
  uint64_t remaining = kMaxSearchNodes;
  bool exceeded = false;
};

/// @brief One level of the feasibility search: the parameter assigned there and
///        the value currently being tried.
struct SearchFrame {
  uint32_t param;
  uint32_t value;
};

/// @brief Backtracking stack owned by the caller so a search allocates nothing.
///
/// Depth is bounded by the parameter count, so a caller that reserves that many
/// frames once can run any number of searches without touching the heap.
using SearchStack = std::vector<SearchFrame>;

/// @brief Smallest valid value index at or after @p start, or the domain size.
uint32_t NextValidValue(const model::Parameter& parameter, uint32_t start) {
  for (uint32_t vi = start; vi < parameter.size(); ++vi) {
    if (!parameter.is_invalid(vi)) return vi;
  }
  return static_cast<uint32_t>(parameter.size());
}

// Independent feasibility oracle. This intentionally does not use the core
// solver so validator agreement cannot be caused by shared implementation.
//
// The search runs on an explicit stack: its depth grows with the parameter
// count, and a satisfiable chain spends only one node of the budget per level,
// so the node budget alone does not bound how deep it goes. Stack use therefore
// stays independent of the model size, while the enumeration order, budget
// accounting and assignment side effects match a recursive descent.
//
// @p assignment is left exactly as it was received on every exit path, so a
// caller can hand over its own scratch buffer instead of copying the partial.
bool ValidatorSearch(const std::vector<model::Parameter>& params,
                     const std::vector<model::Constraint>& constraints,
                     std::vector<uint32_t>& assignment, uint32_t cursor, SearchBudget& budget,
                     SearchStack& stack) {
  stack.clear();
  bool expand = true;

  for (;;) {
    if (expand) {
      expand = false;
      bool dead = false;
      if (budget.remaining == 0) {
        budget.exceeded = true;
        dead = true;
      } else {
        --budget.remaining;
        for (const auto& constraint : constraints) {
          if (constraint->Evaluate(assignment) == model::ConstraintResult::kFalse) {
            dead = true;
            break;
          }
        }
      }
      if (!dead) {
        while (cursor < params.size() && assignment[cursor] != model::kUnassigned) {
          ++cursor;
        }
        if (cursor == params.size()) {
          bool satisfied = true;
          for (const auto& constraint : constraints) {
            if (constraint->Evaluate(assignment) != model::ConstraintResult::kTrue) {
              satisfied = false;
              break;
            }
          }
          if (satisfied) {
            // Undo this search's own writes so the caller's buffer comes back
            // holding exactly the partial assignment it passed in.
            for (const auto& frame : stack) assignment[frame.param] = model::kUnassigned;
            return true;
          }
        } else {
          uint32_t vi = NextValidValue(params[cursor], 0);
          if (vi < params[cursor].size()) {
            assignment[cursor] = vi;
            stack.push_back({cursor, vi});
            ++cursor;
            expand = true;
            continue;
          }
        }
      }
    }

    // Backtrack. An exhausted budget unwinds without trying further values, so
    // the caller sees an untouched assignment together with budget.exceeded.
    if (stack.empty()) return false;
    SearchFrame& top = stack.back();
    uint32_t vi = budget.exceeded ? static_cast<uint32_t>(params[top.param].size())
                                  : NextValidValue(params[top.param], top.value + 1);
    if (vi < params[top.param].size()) {
      top.value = vi;
      assignment[top.param] = vi;
      cursor = top.param + 1;
      expand = true;
      continue;
    }
    assignment[top.param] = model::kUnassigned;
    stack.pop_back();
  }
}

/// @brief Whether @p partial extends to a full valid, constraint-satisfying
///        assignment. @p partial is scratch: it is restored before returning.
bool HasSatisfyingCompletion(const std::vector<model::Parameter>& params,
                             const std::vector<model::Constraint>& constraints,
                             std::vector<uint32_t>& partial, SearchStack& stack,
                             bool* exceeded = nullptr) {
  SearchBudget budget;
  bool result = ValidatorSearch(params, constraints, partial, 0, budget, stack);
  if (exceeded != nullptr) *exceeded = budget.exceeded;
  return result;
}

model::Error ValidateSatisfiableModel(const std::vector<model::Parameter>& params,
                                      const std::vector<model::Constraint>& constraints) {
  if (constraints.empty()) return {};
  std::vector<uint32_t> assignment(params.size(), model::kUnassigned);
  SearchStack stack;
  bool exceeded = false;
  if (HasSatisfyingCompletion(params, constraints, assignment, stack, &exceeded)) return {};
  if (exceeded) {
    return {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
            "The constraint model is too complex to solve within the search budget"};
  }
  return {model::Error::Code::kConstraintError, "Constraints are unsatisfiable",
          "No complete assignment using valid values satisfies all constraints"};
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
  if (!util::CheckedBinomial(n, strength, util::BinomialLimit(kMaxCombinations), combinations)) {
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

  // ValidateParameters enforces the parameter budget (model::kMaxParameters),
  // and it runs before any feasibility search: the search walks one parameter
  // per level, so an oversized model has to be rejected up front rather than
  // discovered part-way through a descent.
  report.error = model::ValidateParameters(params);
  if (!report.error.ok()) return report;

  report.error = ValidateSatisfiableModel(params, constraints);
  if (!report.error.ok()) return report;

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

  // Buffers reused across combinations to avoid per-tuple heap allocation. The
  // search stack is reserved to the maximum depth a search can reach so even
  // the first feasibility check inside the tuple loop cannot grow it.
  std::vector<uint32_t> value_indices(strength);
  std::vector<char> covered_flags;
  SearchStack search_stack;
  search_stack.reserve(n);

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

      // Step 2b: If this tuple has no constraint-satisfying completion, exclude
      // it from the coverage universe entirely (matches the generator's
      // CoverageEngine::ExcludeInvalidTuples semantics).
      //
      // A covered tuple needs no search: the valid test that covers it is
      // itself a complete assignment of valid values satisfying every
      // constraint, so the completion witness is already in hand. Asking the
      // solver again can only reproduce that answer — or fail to reach it
      // within the node budget and report a feasible tuple as undecidable.
      const bool covered = covered_flags[static_cast<size_t>(flat)] != 0;
      if (!covered && !constraints.empty()) {
        for (uint32_t j = 0; j < strength; ++j) {
          assignment[combo[j]] = value_indices[j];
        }
        bool tuple_exceeded = false;
        bool excluded = !HasSatisfyingCompletion(params, constraints, assignment, search_stack,
                                                 &tuple_exceeded);
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
      if (covered) {
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
          uncovered.indices.push_back({pi, vi});
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
enum class ClassTupleFeasibility { kFeasible, kInfeasible, kBudgetExceeded };

/// @brief Marks a value that belongs to no class in a parameter's class domain.
constexpr uint32_t kNoClass = UINT32_MAX;

struct ClassDomain {
  std::vector<std::string> names;
  std::vector<std::vector<uint32_t>> valid_value_indices;
  /// @brief Class index per value index, or kNoClass. Resolving a value to its
  ///        class is a flat array read, never a name hash: the projection below
  ///        runs it once per (combination, test, position).
  std::vector<uint32_t> class_index_by_value;
};

ClassDomain BuildClassDomain(const model::Parameter& parameter) {
  ClassDomain domain;
  domain.class_index_by_value.assign(parameter.size(), kNoClass);
  // Name lookup is confined to this one-off build; nothing downstream hashes.
  std::unordered_map<std::string, uint32_t> index_by_name;
  for (uint32_t vi = 0; vi < parameter.size(); ++vi) {
    if (parameter.is_invalid(vi)) continue;
    const std::string& class_name = parameter.equivalence_class(vi);
    if (class_name.empty()) continue;
    auto [it, inserted] =
        index_by_name.emplace(class_name, static_cast<uint32_t>(domain.names.size()));
    if (inserted) {
      domain.names.push_back(class_name);
      domain.valid_value_indices.emplace_back();
    }
    domain.valid_value_indices[it->second].push_back(vi);
    domain.class_index_by_value[vi] = it->second;
  }
  return domain;
}

model::Error PreflightClassEnumeration(const std::vector<ClassDomain>& domains, uint32_t strength) {
  uint32_t n = static_cast<uint32_t>(domains.size());
  uint64_t combinations = 0;
  if (!util::CheckedBinomial(n, strength, util::BinomialLimit(kMaxCombinations), combinations)) {
    return {model::Error::Code::kTupleExplosion, "class combination metadata exceeds safety limit",
            "Combinations exceed limit: " + std::to_string(kMaxCombinations)};
  }

  std::vector<uint32_t> combo(strength);
  for (uint32_t i = 0; i < strength; ++i) combo[i] = i;
  uint64_t total = 0;
  for (;;) {
    uint64_t product = 1;
    for (uint32_t index : combo) {
      uint64_t radix = domains[index].names.size();
      if (radix != 0 && product > kMaxTuples / radix) {
        return {model::Error::Code::kTupleExplosion,
                "equivalence-class tuple count exceeds safety limit",
                "Total class tuples exceed limit: " + std::to_string(kMaxTuples)};
      }
      product *= radix;
    }
    if (total > kMaxTuples - product) {
      return {model::Error::Code::kTupleExplosion,
              "equivalence-class tuple count exceeds safety limit",
              "Total class tuples exceed limit: " + std::to_string(kMaxTuples)};
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

ClassTupleFeasibility ClassTupleHasValidRepresentative(
    const std::vector<model::Parameter>& params, const std::vector<uint32_t>& class_param_indices,
    const std::vector<const std::vector<uint32_t>*>& candidates,
    const std::vector<model::Constraint>& constraints, std::vector<uint32_t>& assignment,
    std::vector<uint32_t>& choice, SearchStack& stack) {
  uint32_t k = static_cast<uint32_t>(class_param_indices.size());
  choice.assign(k, 0);
  // One representative exhausting its budget says nothing about the others, so
  // it must not end the search: only a feasible representative, or the whole
  // enumeration completing, decides the tuple. An exhausted budget is remembered
  // and reported only when no representative proved feasible, which keeps the
  // verdict independent of the order values appear in.
  bool any_exceeded = false;
  for (;;) {
    for (uint32_t i = 0; i < k; ++i) {
      assignment[class_param_indices[i]] = (*candidates[i])[choice[i]];
    }
    bool exceeded = false;
    bool violated = !HasSatisfyingCompletion(params, constraints, assignment, stack, &exceeded);
    for (uint32_t i = 0; i < k; ++i) {
      assignment[class_param_indices[i]] = model::kUnassigned;
    }
    if (!violated) return ClassTupleFeasibility::kFeasible;
    if (exceeded) any_exceeded = true;

    // Advance the mixed-radix choice vector.
    int pos = static_cast<int>(k) - 1;
    while (pos >= 0) {
      if (++choice[pos] < candidates[pos]->size()) break;
      choice[pos] = 0;
      --pos;
    }
    if (pos < 0) break;
  }
  return any_exceeded ? ClassTupleFeasibility::kBudgetExceeded : ClassTupleFeasibility::kInfeasible;
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

  report.error = model::ValidateParameters(params);
  if (!report.error.ok()) return report;

  report.error = ValidateSatisfiableModel(params, constraints);
  if (!report.error.ok()) return report;

  // Identify parameters that have equivalence classes.
  std::vector<uint32_t> class_params;
  std::vector<ClassDomain> class_domains;
  for (uint32_t i = 0; i < n; ++i) {
    if (params[i].has_equivalence_classes()) {
      class_params.push_back(i);
      class_domains.push_back(BuildClassDomain(params[i]));
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

  report.error = PreflightClassEnumeration(class_domains, effective_strength);
  if (!report.error.ok()) return report;

  // Generate all C(class_n, effective_strength) combinations of class-enabled parameters.
  auto combinations = util::GenerateCombinations(class_n, effective_strength);
  std::vector<const model::TestCase*> valid_tests;
  valid_tests.reserve(tests.size());
  for (const auto& test : tests) {
    if (ValidatePositiveTest(test, params, constraints).empty()) valid_tests.push_back(&test);
  }

  std::vector<uint32_t> class_indices(effective_strength);
  std::vector<uint32_t> combo_param_indices(effective_strength);
  std::vector<const std::vector<uint32_t>*> required_candidates(effective_strength);
  std::vector<uint32_t> assignment(params.size(), model::kUnassigned);
  std::vector<uint32_t> choice;
  std::vector<char> covered_flags;
  SearchStack search_stack;
  search_stack.reserve(params.size());

  // For each combination, enumerate all class tuples.
  for (const auto& combo : combinations) {
    // Resolve the global parameter indices for this class combination once.
    for (uint32_t k = 0; k < effective_strength; ++k) {
      combo_param_indices[k] = class_params[combo[k]];
    }

    // Compute the number of class tuples for this combination. Accumulate in
    // 64-bit so a large class product cannot silently wrap.
    uint64_t num_tuples = 1;
    for (uint32_t index : combo) {
      num_tuples *= class_domains[index].names.size();
    }

    // Project every valid test to one class-tuple flag once, instead of
    // rescanning the full test suite for every class tuple.
    covered_flags.assign(static_cast<size_t>(num_tuples), 0);
    for (const auto* test : valid_tests) {
      uint64_t projected = 0;
      bool in_domain = true;
      for (uint32_t k = 0; k < effective_strength; ++k) {
        const ClassDomain& domain = class_domains[combo[k]];
        uint32_t class_index = domain.class_index_by_value[test->values[class_params[combo[k]]]];
        if (class_index == kNoClass) {
          in_domain = false;
          break;
        }
        projected = projected * domain.names.size() + class_index;
      }
      if (in_domain) covered_flags[static_cast<size_t>(projected)] = 1;
    }

    // Enumerate all class tuples and check coverage.
    for (uint64_t flat = 0; flat < num_tuples; ++flat) {
      // Decode flat index into class indices.
      uint64_t remainder = flat;
      for (int i = static_cast<int>(effective_strength) - 1; i >= 0; --i) {
        uint32_t radix = static_cast<uint32_t>(class_domains[combo[i]].valid_value_indices.size());
        class_indices[i] = static_cast<uint32_t>(remainder % radix);
        remainder /= radix;
      }

      if (!constraints.empty()) {
        for (uint32_t k = 0; k < effective_strength; ++k) {
          required_candidates[k] = &class_domains[combo[k]].valid_value_indices[class_indices[k]];
        }
        auto feasibility =
            ClassTupleHasValidRepresentative(params, combo_param_indices, required_candidates,
                                             constraints, assignment, choice, search_stack);
        if (feasibility == ClassTupleFeasibility::kBudgetExceeded) {
          report.error = {
              model::Error::Code::kConstraintError, "Constraint search budget exceeded",
              "Class-tuple feasibility could not be determined within the search budget"};
          return report;
        }
        if (feasibility == ClassTupleFeasibility::kInfeasible) continue;
      }

      ++report.total_class_tuples;
      if (covered_flags[static_cast<size_t>(flat)]) ++report.covered_class_tuples;
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
  if (!class_report.error.ok()) {
    if (result.error.ok()) result.error = class_report.error;
    result.warnings.push_back(class_report.error.message + ": " + class_report.error.detail);
    return;
  }
  result.class_coverage = model::ClassCoverage{
      class_report.total_class_tuples,
      class_report.covered_class_tuples,
      class_report.coverage_ratio,
  };
}

}  // namespace validator
}  // namespace coverwise
