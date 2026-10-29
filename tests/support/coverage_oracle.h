/// @file coverage_oracle.h
/// @brief Recompute a generated suite's coverage without the generator.
///
/// Every assertion about the completeness of a Generate/Extend result goes
/// through here, so it is judged by validator::ValidateCoverage and by a
/// brute-force enumeration of the model rather than by the coverage the result
/// reports about itself. Nothing here reads generator internals.
///
/// Sub-model and negative universes are enumerated from full assignments, so
/// they are meant for the small models unit tests use; a model whose assignment
/// space exceeds kMaxOracleAssignments is refused with an error.

#ifndef COVERWISE_TESTS_SUPPORT_COVERAGE_ORACLE_H_
#define COVERWISE_TESTS_SUPPORT_COVERAGE_ORACLE_H_

#include <gtest/gtest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/generate_options.h"
#include "model/test_case.h"
#include "util/combinatorics.h"
#include "validator/coverage_validator.h"

namespace coverwise {
namespace test_support {

/// @brief Largest assignment space the enumerated universes will walk.
inline constexpr uint64_t kMaxOracleAssignments = 1000000;

/// @brief A suite's coverage as recomputed from its rows alone.
struct OracleCoverage {
  /// @brief The global model, from validator::ValidateCoverage.
  validator::CoverageReport global;
  /// @brief Minimum ratio over the global model and every sub-model.
  double ratio = 0.0;
  /// @brief Feasible single-fault tuples, and how many any single-fault row hits.
  uint64_t negative_total = 0;
  uint64_t negative_covered = 0;
  /// @brief Indices into negative_tests of rows that do not hold exactly one
  /// invalid value or that violate a constraint.
  std::vector<size_t> malformed_negative_rows;
  model::Error error;
};

namespace detail {

/// @brief The single invalid position of @p values, or params.size() if there is not exactly one.
inline size_t SingleFaultPosition(const std::vector<uint32_t>& values,
                                  const std::vector<model::Parameter>& params) {
  if (values.size() != params.size()) return params.size();
  size_t fault = params.size();
  for (size_t pi = 0; pi < params.size(); ++pi) {
    if (values[pi] >= params[pi].size()) return params.size();
    if (!params[pi].is_invalid(values[pi])) continue;
    if (fault != params.size()) return params.size();
    fault = pi;
  }
  return fault;
}

inline bool SatisfiesAll(const std::vector<uint32_t>& values,
                         const std::vector<model::Constraint>& constraints) {
  for (const auto& constraint : constraints) {
    if (constraint->Evaluate(values) != model::ConstraintResult::kTrue) return false;
  }
  return true;
}

/// @brief Identity of the tuple @p values projects to on @p combo.
inline std::vector<uint32_t> TupleKey(const std::vector<uint32_t>& combo,
                                      const std::vector<uint32_t>& values) {
  std::vector<uint32_t> key;
  key.reserve(combo.size() * 2);
  for (uint32_t pi : combo) {
    key.push_back(pi);
    key.push_back(values[pi]);
  }
  return key;
}

/// @brief Every tuple on @p combos that some constraint-satisfying assignment
/// drawn from @p domains projects to.
inline bool FeasibleTuples(const std::vector<std::vector<uint32_t>>& domains,
                           const std::vector<std::vector<uint32_t>>& combos,
                           const std::vector<model::Constraint>& constraints,
                           std::set<std::vector<uint32_t>>& feasible) {
  uint64_t space = 1;
  for (const auto& domain : domains) {
    if (domain.empty()) return true;
    space *= domain.size();
    if (space > kMaxOracleAssignments) return false;
  }
  const size_t n = domains.size();
  std::vector<size_t> cursor(n, 0);
  std::vector<uint32_t> assignment(n);
  for (bool more = true; more;) {
    for (size_t pi = 0; pi < n; ++pi) assignment[pi] = domains[pi][cursor[pi]];
    if (SatisfiesAll(assignment, constraints)) {
      for (const auto& combo : combos) feasible.insert(TupleKey(combo, assignment));
    }
    more = false;
    for (size_t pi = 0; pi < n; ++pi) {
      if (++cursor[pi] < domains[pi].size()) {
        more = true;
        break;
      }
      cursor[pi] = 0;
    }
  }
  return true;
}

/// @brief Valid values of every parameter, with @p fixed pinned to @p fixed_value if given.
inline std::vector<std::vector<uint32_t>> ValidDomains(const std::vector<model::Parameter>& params,
                                                       size_t fixed, uint32_t fixed_value) {
  std::vector<std::vector<uint32_t>> domains(params.size());
  for (size_t pi = 0; pi < params.size(); ++pi) {
    if (pi == fixed) {
      domains[pi] = {fixed_value};
      continue;
    }
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (!params[pi].is_invalid(vi)) domains[pi].push_back(vi);
    }
  }
  return domains;
}

/// @brief How many of the feasible tuples on @p combos the rows hit.
inline uint64_t CoveredCount(const std::set<std::vector<uint32_t>>& feasible,
                             const std::vector<std::vector<uint32_t>>& combos,
                             const std::vector<const model::TestCase*>& rows) {
  std::set<std::vector<uint32_t>> covered;
  for (const auto* row : rows) {
    for (const auto& combo : combos) {
      auto key = TupleKey(combo, row->values);
      if (feasible.count(key) > 0) covered.insert(std::move(key));
    }
  }
  return covered.size();
}

}  // namespace detail

/// @brief Recompute the coverage of @p result for the model @p options describes.
///
/// Constraints are parsed against `result.parameters`, the value space the rows
/// address. A preserved single-fault row in `tests` counts toward negative
/// coverage, as it does in the engine.
inline OracleCoverage RecomputeCoverage(const model::GenerateResult& result,
                                        const model::GenerateOptions& options) {
  OracleCoverage oracle;
  const auto& params = result.parameters;
  const auto n = static_cast<uint32_t>(params.size());
  std::vector<model::Constraint> constraints;
  for (const auto& expression : options.constraint_expressions) {
    auto parsed = model::ParseConstraint(expression, params);
    if (!parsed.error.ok()) {
      oracle.error = parsed.error;
      return oracle;
    }
    constraints.push_back(std::move(parsed.constraint));
  }
  const model::Error too_large{model::Error::Code::kTupleExplosion,
                               "model too large for the coverage oracle", ""};

  oracle.global = validator::ValidateCoverage(params, result.tests, options.strength, constraints);
  if (!oracle.global.error.ok()) {
    oracle.error = oracle.global.error;
    return oracle;
  }
  oracle.ratio = oracle.global.coverage_ratio;

  std::vector<bool> rejected(result.tests.size(), false);
  for (const auto& invalid : oracle.global.invalid_tests) rejected[invalid.test_index] = true;
  std::vector<const model::TestCase*> accepted_rows;
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    if (!rejected[ti]) accepted_rows.push_back(&result.tests[ti]);
  }
  for (const auto& sub_model : options.sub_models) {
    std::vector<uint32_t> indices;
    for (const auto& name : sub_model.parameter_names) {
      for (uint32_t pi = 0; pi < n; ++pi) {
        if (params[pi].name == name) indices.push_back(pi);
      }
    }
    std::sort(indices.begin(), indices.end());
    std::vector<std::vector<uint32_t>> combos;
    for (const auto& local :
         util::GenerateCombinations(static_cast<uint32_t>(indices.size()), sub_model.strength)) {
      std::vector<uint32_t> combo;
      for (uint32_t k : local) combo.push_back(indices[k]);
      combos.push_back(std::move(combo));
    }
    std::set<std::vector<uint32_t>> feasible;
    if (!detail::FeasibleTuples(detail::ValidDomains(params, n, 0), combos, constraints,
                                feasible)) {
      oracle.error = too_large;
      return oracle;
    }
    if (!feasible.empty()) {
      const uint64_t covered = detail::CoveredCount(feasible, combos, accepted_rows);
      oracle.ratio = std::min(oracle.ratio,
                              static_cast<double>(covered) / static_cast<double>(feasible.size()));
    }
  }

  for (size_t ri = 0; ri < result.negative_tests.size(); ++ri) {
    const auto& values = result.negative_tests[ri].values;
    if (detail::SingleFaultPosition(values, params) == params.size() ||
        !detail::SatisfiesAll(values, constraints)) {
      oracle.malformed_negative_rows.push_back(ri);
    }
  }
  const auto all_combos = util::GenerateCombinations(n, options.strength);
  for (uint32_t fixed = 0; fixed < n; ++fixed) {
    std::vector<std::vector<uint32_t>> combos;
    for (const auto& combo : all_combos) {
      if (std::find(combo.begin(), combo.end(), fixed) != combo.end()) combos.push_back(combo);
    }
    for (uint32_t fixed_value = 0; fixed_value < params[fixed].size(); ++fixed_value) {
      if (!params[fixed].is_invalid(fixed_value)) continue;
      std::set<std::vector<uint32_t>> feasible;
      if (!detail::FeasibleTuples(detail::ValidDomains(params, fixed, fixed_value), combos,
                                  constraints, feasible)) {
        oracle.error = too_large;
        return oracle;
      }
      std::vector<const model::TestCase*> rows;
      for (const auto* suite : {&result.negative_tests, &result.tests}) {
        for (const auto& row : *suite) {
          if (detail::SingleFaultPosition(row.values, params) == fixed &&
              row.values[fixed] == fixed_value && detail::SatisfiesAll(row.values, constraints)) {
            rows.push_back(&row);
          }
        }
      }
      oracle.negative_total += feasible.size();
      oracle.negative_covered += detail::CoveredCount(feasible, combos, rows);
    }
  }
  return oracle;
}

/// @brief Whether the oracle agrees with every coverage figure @p result reports.
///
/// Positive coverage must match the reported ratio, and when the result carries
/// negative coverage its counts must match the recount and every negative row
/// must hold exactly one invalid value and satisfy the constraints.
inline ::testing::AssertionResult MatchesOracle(const model::GenerateResult& result,
                                                const model::GenerateOptions& options) {
  const auto oracle = RecomputeCoverage(result, options);
  if (!oracle.error.ok()) {
    return ::testing::AssertionFailure() << "oracle error: " << oracle.error.message;
  }
  if (oracle.ratio != result.coverage) {
    return ::testing::AssertionFailure()
           << "oracle coverage " << oracle.ratio << " != reported " << result.coverage;
  }
  if (!oracle.malformed_negative_rows.empty()) {
    return ::testing::AssertionFailure()
           << "negative row " << oracle.malformed_negative_rows.front()
           << " does not hold exactly one invalid value or violates a constraint";
  }
  if (result.negative_coverage.has_value()) {
    const auto& negative = *result.negative_coverage;
    if (negative.total_tuples != oracle.negative_total ||
        negative.covered_tuples != oracle.negative_covered) {
      return ::testing::AssertionFailure()
             << "negative coverage " << negative.covered_tuples << "/" << negative.total_tuples
             << " != oracle " << oracle.negative_covered << "/" << oracle.negative_total;
    }
  }
  return ::testing::AssertionSuccess();
}

/// @brief Whether @p result is a complete suite by the oracle's count and its own.
inline ::testing::AssertionResult IsCompleteByOracle(const model::GenerateResult& result,
                                                     const model::GenerateOptions& options) {
  auto matches = MatchesOracle(result, options);
  if (!matches) return matches;
  if (result.coverage != 1.0) {
    return ::testing::AssertionFailure() << "coverage " << result.coverage << " < 1";
  }
  return ::testing::AssertionSuccess();
}

}  // namespace test_support
}  // namespace coverwise

#endif  // COVERWISE_TESTS_SUPPORT_COVERAGE_ORACLE_H_
