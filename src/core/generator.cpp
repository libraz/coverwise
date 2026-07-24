/// @file generator.cpp

#include "core/generator.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "algo/greedy.h"
#include "core/constraint_solver.h"
#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/options_validation.h"
#include "util/rng.h"
#include "util/string_util.h"
#include "validator/coverage_validator.h"

namespace coverwise {
namespace core {

using model::ExtendMode;
using model::GenerateOptions;
using model::ModelStats;
using model::SubModel;
using model::WeightConfig;

namespace {

/// @brief Resolve parameter names to indices.
/// @return Empty vector and error message if any name is not found.
std::pair<std::vector<uint32_t>, std::string> ResolveParamNames(
    const std::vector<std::string>& names, const std::vector<model::Parameter>& params) {
  std::vector<uint32_t> indices;
  indices.reserve(names.size());
  for (const auto& name : names) {
    bool found = false;
    for (uint32_t i = 0; i < static_cast<uint32_t>(params.size()); ++i) {
      if (params[i].name == name) {
        indices.push_back(i);
        found = true;
        break;
      }
    }
    if (!found) {
      return {{}, "Unknown parameter in sub-model: " + name};
    }
  }
  // Sort for consistent combination generation.
  std::sort(indices.begin(), indices.end());
  return {indices, {}};
}

/// @brief Check if all engines are complete.
bool AllComplete(const CoverageEngine& global, const std::vector<CoverageEngine>& sub_engines) {
  if (!global.IsComplete()) return false;
  for (const auto& eng : sub_engines) {
    if (!eng.IsComplete()) return false;
  }
  return true;
}

/// @brief Sum ScoreCandidate across all engines.
uint32_t TotalScore(const CoverageEngine& global, const std::vector<CoverageEngine>& sub_engines,
                    const model::TestCase& tc) {
  uint32_t score = global.ScoreCandidate(tc);
  for (const auto& eng : sub_engines) {
    score += eng.ScoreCandidate(tc);
  }
  return score;
}

/// @brief Build an allowed_values mask that only permits valid values.
std::vector<std::vector<bool>> BuildValidOnlyMask(const std::vector<model::Parameter>& params) {
  std::vector<std::vector<bool>> mask(params.size());
  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    mask[pi].resize(params[pi].size(), true);
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (params[pi].is_invalid(vi)) {
        mask[pi][vi] = false;
      }
    }
  }
  return mask;
}

/// @brief Validate that a seed can participate in positive coverage.
std::string ValidatePositiveSeed(const model::TestCase& seed,
                                 const std::vector<model::Parameter>& params,
                                 const std::vector<model::Constraint>& constraints) {
  if (seed.values.size() != params.size()) {
    return "expected " + std::to_string(params.size()) + " value(s), got " +
           std::to_string(seed.values.size());
  }

  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    uint32_t vi = seed.values[pi];
    if (vi >= params[pi].size()) {
      return "value index " + std::to_string(vi) + " is out of range for parameter " +
             params[pi].name;
    }
    if (params[pi].is_invalid(vi)) {
      return "value " + params[pi].name + "=" + params[pi].values[vi] + " is marked invalid";
    }
  }

  for (const auto& constraint : constraints) {
    if (constraint->Evaluate(seed.values) != model::ConstraintResult::kTrue) {
      return "violates a constraint";
    }
  }

  return {};
}

/// @brief Build an allowed_values mask for negative test generation.
///
/// The fixed parameter is allowed only at the given invalid value index.
/// All other parameters are allowed only at their valid values.
std::vector<std::vector<bool>> BuildNegativeMask(const std::vector<model::Parameter>& params,
                                                 uint32_t fixed_param, uint32_t fixed_value) {
  std::vector<std::vector<bool>> mask(params.size());
  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    mask[pi].resize(params[pi].size(), false);
    if (pi == fixed_param) {
      mask[pi][fixed_value] = true;
    } else {
      for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
        if (!params[pi].is_invalid(vi)) {
          mask[pi][vi] = true;
        }
      }
    }
  }
  return mask;
}

/// @brief Generate deterministic single-fault negative coverage.
///
/// Reuses the positive engine after its metrics have been collected, so the
/// negative pass never adds a second full tuple bitmap to peak memory.
model::Error GenerateNegativeTests(const std::vector<model::Parameter>& params,
                                   const std::vector<model::Constraint>& constraints,
                                   CoverageEngine& fresh_cov, uint32_t max_tests,
                                   size_t positive_test_count,
                                   std::vector<model::TestCase>& negative_tests,
                                   model::NegativeCoverage& metrics,
                                   std::vector<std::string>& warnings) {
  bool stopped_at_max_tests = false;

  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (!params[pi].is_invalid(vi)) continue;

      // Reset coverage/exclusion state for this invalid value, reusing the
      // precomputed tables.
      fresh_cov.ResetCoverage();

      // Build mask: pi can only be vi, others can only be valid.
      auto neg_mask = BuildNegativeMask(params, pi, vi);
      fresh_cov.ExcludeTuplesOutsideMask(neg_mask);
      fresh_cov.ExcludeTuplesNotContaining(pi, vi);
      bool exclusion_budget_exceeded = false;
      fresh_cov.ExcludeInvalidTuples(constraints, neg_mask, &exclusion_budget_exceeded);
      if (exclusion_budget_exceeded) {
        return {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
                "Negative coverage targets could not be classified within the search budget"};
      }
      const bool no_feasible_target = fresh_cov.TotalTuples() == 0;

      // Cover every feasible requested-strength tuple containing the fixed
      // invalid value. FirstUncovered + CompleteAssignment is the same
      // deterministic completion path used by positive generation.
      while (!fresh_cov.IsComplete()) {
        if (max_tests > 0 &&
            positive_test_count + negative_tests.size() >= static_cast<size_t>(max_tests)) {
          stopped_at_max_tests = true;
          break;
        }
        CoverageEngine::UncoveredAssignment uncovered;
        if (!fresh_cov.FirstUncovered(uncovered)) break;
        model::TestCase witness{std::move(uncovered.assignment)};
        SolveBudget budget;
        if (!CompleteAssignment(params, constraints, neg_mask, witness, &budget)) {
          if (budget.exceeded) {
            return {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
                    "A negative coverage witness could not be found within the search budget"};
          }
          fresh_cov.ExcludeTuple(uncovered.index);
          continue;
        }
        fresh_cov.AddTestCase(witness);
        negative_tests.push_back(std::move(witness));
      }

      metrics.total_tuples += fresh_cov.TotalTuples();
      metrics.covered_tuples += fresh_cov.CoveredCount();
      if (!fresh_cov.IsComplete() || no_feasible_target) {
        warnings.push_back("Negative coverage incomplete for " + params[pi].name + "=" +
                           params[pi].values[vi]);
      }
    }
  }
  metrics.omitted_tuples = metrics.total_tuples - metrics.covered_tuples;
  metrics.coverage_ratio =
      metrics.total_tuples == 0
          ? 1.0
          : static_cast<double>(metrics.covered_tuples) / static_cast<double>(metrics.total_tuples);
  if (stopped_at_max_tests) {
    warnings.push_back("Negative generation stopped at maxTests (" + std::to_string(max_tests) +
                       ") before reaching full coverage");
  }
  return {};
}

/// @brief Resolve string-based WeightConfig to index-based weight vectors.
/// @return weights[param_idx][value_idx] = weight (default 1.0).
///         Empty vector if no weights are configured.
std::vector<std::vector<double>> ResolveWeights(const std::vector<model::Parameter>& params,
                                                const WeightConfig& config) {
  if (config.empty()) return {};
  std::vector<std::vector<double>> resolved(params.size());
  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    resolved[pi].resize(params[pi].size(), 1.0);
    auto pit = config.entries.find(params[pi].name);
    if (pit == config.entries.end()) continue;
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      // Resolve by key presence (not GetWeight's 1.0 sentinel) so an explicit
      // weight of 1.0 is honored and a weight keyed by one of the value's
      // aliases is not silently dropped to the default.
      auto vit = pit->second.find(params[pi].values[vi]);
      if (vit == pit->second.end()) {
        for (const auto& alias : params[pi].aliases(vi)) {
          vit = pit->second.find(alias);
          if (vit != pit->second.end()) break;
        }
      }
      if (vit != pit->second.end()) {
        resolved[pi][vi] = vit->second;
      }
    }
  }
  return resolved;
}

/// @brief Apply boundary value expansion to parameters with boundary configs.
void ApplyBoundaryExpansion(GenerateOptions& opts) {
  if (opts.boundary_configs.empty()) return;
  const auto original_params = opts.parameters;
  for (auto& param : opts.parameters) {
    auto it = opts.boundary_configs.find(param.name);
    if (it != opts.boundary_configs.end()) {
      param = model::ExpandBoundaryValues(param, it->second);
    }
  }

  // Test cases carry indices into the input value arrays. Boundary expansion
  // sorts and inserts values, so remap every valid old index by value identity.
  for (auto& test : opts.seeds) {
    for (size_t pi = 0; pi < test.values.size() && pi < original_params.size(); ++pi) {
      const uint32_t old_index = test.values[pi];
      if (old_index >= original_params[pi].size()) continue;
      const auto& old_value = original_params[pi].values[old_index];
      uint32_t new_index = opts.parameters[pi].find_value_index(old_value);
      if (new_index == model::kUnassigned && util::IsNumeric(old_value)) {
        const double numeric = util::ToDouble(old_value);
        for (uint32_t vi = 0; vi < opts.parameters[pi].size(); ++vi) {
          const auto& candidate = opts.parameters[pi].values[vi];
          if (util::IsNumeric(candidate) && util::ToDouble(candidate) == numeric) {
            new_index = vi;
            break;
          }
        }
      }
      if (new_index != model::kUnassigned) test.values[pi] = new_index;
    }
  }
}

}  // namespace

model::GenerateResult GenerateImpl(const GenerateOptions& options, size_t preserved_seed_count) {
  model::GenerateResult result;
  result.parameters = options.parameters;

  result.error = model::ValidateGenerateOptions(options);
  if (!result.error.ok()) {
    result.warnings.push_back(result.error.message +
                              (result.error.detail.empty() ? "" : ": " + result.error.detail));
    return result;
  }

  // Apply boundary value expansion to parameters that have boundary configs.
  GenerateOptions opts = options;
  ApplyBoundaryExpansion(opts);
  result.parameters = opts.parameters;
  auto expanded_param_error = model::ValidateParameters(opts.parameters);
  if (!expanded_param_error.ok()) {
    result.error = expanded_param_error;
    result.warnings.push_back(result.error.message);
    return result;
  }

  bool has_invalid = model::HasInvalidValues(opts.parameters);

  auto coverage_result = CoverageEngine::Create(opts.parameters, opts.strength);
  if (!coverage_result.second.ok()) {
    result.warnings.push_back(coverage_result.second.message + ": " +
                              coverage_result.second.detail);
    result.error = coverage_result.second;
    return result;
  }
  auto coverage = std::move(coverage_result.first);
  uint64_t allocated_tuples = coverage.TotalTuples();

  // Create sub-model engines.
  std::vector<CoverageEngine> sub_engines;
  for (const auto& sm : opts.sub_models) {
    auto [indices, resolve_err] = ResolveParamNames(sm.parameter_names, opts.parameters);
    if (!resolve_err.empty()) {
      result.warnings.push_back(resolve_err);
      result.error = {model::Error::Code::kInvalidInput, resolve_err, ""};
      return result;
    }
    if (indices.size() < sm.strength) {
      std::string msg = "Sub-model strength (" + std::to_string(sm.strength) +
                        ") exceeds parameter count (" + std::to_string(indices.size()) + ")";
      result.warnings.push_back(msg);
      result.error = {model::Error::Code::kInvalidInput, msg, ""};
      return result;
    }
    auto [eng, sm_err] = CoverageEngine::Create(opts.parameters, indices, sm.strength);
    if (!sm_err.ok()) {
      result.warnings.push_back(sm_err.message + ": " + sm_err.detail);
      result.error = sm_err;
      return result;
    }
    if (eng.TotalTuples() > CoverageEngine::kMaxTuples - allocated_tuples) {
      result.error = {model::Error::Code::kTupleExplosion,
                      "Combined global and sub-model tuple count exceeds safe limit",
                      "limit=" + std::to_string(CoverageEngine::kMaxTuples)};
      result.warnings.push_back(result.error.message + ": " + result.error.detail);
      return result;
    }
    allocated_tuples += eng.TotalTuples();
    sub_engines.push_back(std::move(eng));
  }

  // Parse constraint expressions into AST.
  std::vector<model::Constraint> constraints;
  for (const auto& expr : opts.constraint_expressions) {
    auto parse_result = model::ParseConstraint(expr, opts.parameters);
    if (!parse_result.error.ok()) {
      model::Error err = model::AnnotateConstraintError(expr, parse_result.error);
      result.warnings.push_back(err.message + ": " + err.detail);
      result.error = err;
      return result;
    }
    constraints.push_back(std::move(parse_result.constraint));
  }

  // Reject a contradictory model before tuple accounting or generation. This
  // also supplies the same valid-value semantics used by tuple feasibility.
  // The search is node-bounded; an exhausted budget is reported explicitly
  // rather than being silently treated as "unsatisfiable".
  if (!constraints.empty()) {
    model::TestCase witness;
    witness.values.assign(opts.parameters.size(), model::kUnassigned);
    SolveBudget budget;
    if (!CompleteValidAssignment(opts.parameters, constraints, witness, &budget)) {
      if (budget.exceeded) {
        result.error = {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
                        "The constraint model is too complex to solve within the search budget"};
      } else {
        result.error = {model::Error::Code::kConstraintError, "Constraints are unsatisfiable",
                        "No complete assignment using valid values satisfies all constraints"};
      }
      result.warnings.push_back(result.error.message + ": " + result.error.detail);
      return result;
    }
  }

  // Exclude tuples that are inherently invalid due to constraints.
  bool exclude_budget_exceeded = false;
  coverage.ExcludeInvalidTuples(constraints, {}, &exclude_budget_exceeded);
  for (auto& eng : sub_engines) {
    eng.ExcludeInvalidTuples(constraints, {}, &exclude_budget_exceeded);
  }
  if (exclude_budget_exceeded) {
    result.error = {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
                    "Tuple feasibility could not be determined within the search budget"};
    result.warnings.push_back(result.error.message + ": " + result.error.detail);
    return result;
  }

  // Exclude tuples involving invalid values for positive generation.
  if (has_invalid) {
    coverage.ExcludeInvalidValues();
    for (auto& eng : sub_engines) {
      eng.ExcludeInvalidValues();
    }
  }

  // Build allowed_values mask for positive generation (valid values only).
  std::vector<std::vector<bool>> valid_mask;
  if (has_invalid) {
    valid_mask = BuildValidOnlyMask(opts.parameters);
  }

  // Resolve value weights to index-based vectors.
  auto resolved_weights = ResolveWeights(opts.parameters, opts.weights);

  util::Rng rng(opts.seed);

  // Pre-load seed tests into all engines. In strict extension mode the existing
  // prefix is retained byte-for-byte even when it no longer matches the model;
  // invalid preserved rows are excluded from coverage accounting.
  bool dropped_for_max_tests = false;
  for (size_t si = 0; si < opts.seeds.size(); ++si) {
    const auto& seed_test = opts.seeds[si];
    if (opts.max_tests > 0 && result.tests.size() >= static_cast<size_t>(opts.max_tests)) {
      dropped_for_max_tests = true;
      break;
    }
    auto seed_error = ValidatePositiveSeed(seed_test, opts.parameters, constraints);
    if (!seed_error.empty()) {
      if (si < preserved_seed_count) {
        result.tests.push_back(seed_test);
        result.warnings.push_back("Existing test " + std::to_string(si) +
                                  " preserved but excluded from coverage: " + seed_error);
      } else {
        result.warnings.push_back("Seed test " + std::to_string(si - preserved_seed_count) +
                                  " ignored: " + seed_error);
      }
      continue;
    }
    coverage.AddTestCase(seed_test);
    for (auto& eng : sub_engines) {
      eng.AddTestCase(seed_test);
    }
    result.tests.push_back(seed_test);
  }
  if (dropped_for_max_tests) {
    result.warnings.push_back("Seed test count (" + std::to_string(opts.seeds.size()) +
                              ") exceeds maxTests (" + std::to_string(opts.max_tests) +
                              "); some seeds were dropped");
  }

  // Scoring lambdas: avoid std::function wrapper on the hot path.
  auto simple_score_fn = [&](const model::TestCase& partial, uint32_t pi, uint32_t vi) {
    return coverage.ScoreValue(partial, pi, vi);
  };
  auto combined_score_fn = [&](const model::TestCase& partial, uint32_t pi,
                               uint32_t vi) -> uint32_t {
    uint32_t score = coverage.ScoreValue(partial, pi, vi);
    for (const auto& eng : sub_engines) {
      score += eng.ScoreValue(partial, pi, vi);
    }
    return score;
  };

  // Constructive greedy generation loop (positive tests only).
  constexpr uint32_t kMaxRetries = 50;
  uint32_t retries = 0;
  while (!AllComplete(coverage, sub_engines) &&
         (opts.max_tests == 0 || result.tests.size() < static_cast<size_t>(opts.max_tests))) {
    auto tc_opt = sub_engines.empty()
                      ? algo::GreedyConstruct(opts.parameters, simple_score_fn, constraints, rng,
                                              valid_mask, resolved_weights)
                      : algo::GreedyConstruct(opts.parameters, combined_score_fn, constraints, rng,
                                              valid_mask, resolved_weights);
    // A failed construction (no constraint-satisfying value for some parameter)
    // is treated like a zero-score candidate: retry with a different shuffle.
    if (!tc_opt) {
      if (++retries >= kMaxRetries) break;
      continue;
    }
    auto& tc = *tc_opt;
    uint32_t score = TotalScore(coverage, sub_engines, tc);
    if (score == 0) {
      if (++retries >= kMaxRetries) break;
      continue;
    }
    retries = 0;
    coverage.AddTestCase(tc);
    for (auto& eng : sub_engines) {
      eng.AddTestCase(tc);
    }
    result.tests.push_back(std::move(tc));
  }

  // Deterministic completion phase. Randomized greedy construction can stall on
  // hard-to-reach tuples (notably t == parameter count, or tightly constrained
  // models), abandoning tuples that are in fact coverable and leaving coverage
  // below 100%. For each remaining uncovered tuple, build a test that covers it
  // directly by fixing the tuple's values and completing the rest with a
  // constraint feasibility search. A tuple that cannot be completed is genuinely
  // infeasible and is excluded from the coverage target so it no longer counts
  // as a shortfall. This runs after greedy so the common case keeps a small
  // suite while completeness is still guaranteed for every feasible tuple.
  bool completion_budget_exceeded = false;
  auto complete_partial = [&](model::TestCase& witness) {
    SolveBudget budget;
    bool ok = has_invalid
                  ? CompleteAssignment(opts.parameters, constraints, valid_mask, witness, &budget)
                  : CompleteValidAssignment(opts.parameters, constraints, witness, &budget);
    if (budget.exceeded) completion_budget_exceeded = true;
    return ok;
  };
  {
    auto pick_incomplete = [&]() -> CoverageEngine* {
      if (!coverage.IsComplete()) return &coverage;
      for (auto& eng : sub_engines) {
        if (!eng.IsComplete()) return &eng;
      }
      return nullptr;
    };
    CoverageEngine* eng = nullptr;
    while ((eng = pick_incomplete()) != nullptr) {
      if (opts.max_tests > 0 && result.tests.size() >= static_cast<size_t>(opts.max_tests)) break;
      CoverageEngine::UncoveredAssignment ua;
      if (!eng->FirstUncovered(ua)) break;  // Defensive: IsComplete() disagreed.
      model::TestCase witness{ua.assignment};
      bool completed = complete_partial(witness);
      if (completion_budget_exceeded) break;
      if (!completed) {
        // Partial-feasible but not extensible to a full satisfying assignment.
        eng->ExcludeTuple(ua.index);
        continue;
      }
      coverage.AddTestCase(witness);
      for (auto& e : sub_engines) {
        e.AddTestCase(witness);
      }
      result.tests.push_back(std::move(witness));
    }
  }
  if (completion_budget_exceeded) {
    result.error = {model::Error::Code::kConstraintError, "Constraint search budget exceeded",
                    "A coverage witness could not be found within the search budget"};
    result.warnings.push_back(result.error.message + ": " + result.error.detail);
    return result;
  }

  // Warn if generation stopped before reaching full coverage. After the
  // completion phase this can only happen when maxTests bounds the suite.
  if (!AllComplete(coverage, sub_engines)) {
    if (opts.max_tests > 0 && result.tests.size() >= static_cast<size_t>(opts.max_tests)) {
      result.warnings.push_back("Generation stopped at maxTests (" +
                                std::to_string(opts.max_tests) + ") before reaching 100% coverage");
    } else {
      result.warnings.push_back("Generation stopped before reaching 100% coverage");
    }
  }

  // Collect uncovered tuples from all engines.
  if (!AllComplete(coverage, sub_engines)) {
    result.uncovered_count = coverage.TotalTuples() - coverage.CoveredCount();
    for (const auto& eng : sub_engines) {
      result.uncovered_count += eng.TotalTuples() - eng.CoveredCount();
    }

    auto global_uncovered = coverage.GetUncoveredTuples(opts.parameters);
    result.uncovered.insert(result.uncovered.end(), global_uncovered.begin(),
                            global_uncovered.end());
    for (const auto& eng : sub_engines) {
      uint32_t remaining = result.uncovered.size() >= CoverageEngine::kMaxDiagnosticTuples
                               ? 0
                               : CoverageEngine::kMaxDiagnosticTuples - result.uncovered.size();
      auto sub_uncovered = eng.GetUncoveredTuples(opts.parameters, remaining);
      result.uncovered.insert(result.uncovered.end(), sub_uncovered.begin(), sub_uncovered.end());
    }
    result.omitted_uncovered = result.uncovered_count - result.uncovered.size();

    constexpr size_t kMaxSuggestions = 100;
    for (const auto& ut : result.uncovered) {
      if (result.suggestions.size() >= kMaxSuggestions) break;
      model::TestCase witness;
      witness.values.assign(opts.parameters.size(), model::kUnassigned);
      // Reconstruct the witness from (param, value) indices rather than parsing
      // "name=value" strings, which is ambiguous when a name or value holds '='.
      for (const auto& [pi, vi] : ut.indices) {
        if (pi < witness.values.size()) witness.values[pi] = vi;
      }
      if (!CompleteValidAssignment(opts.parameters, constraints, witness)) continue;
      model::Suggestion suggestion;
      suggestion.description = "Add test: " + ut.ToString();
      suggestion.test_case = std::move(witness);
      result.suggestions.push_back(std::move(suggestion));
    }
  }

  // Report coverage as the minimum across all engines (for pass/fail).
  // Note: stats.total_tuples and stats.covered_tuples are SUMS across all
  // engines, so stats.covered_tuples / stats.total_tuples may differ from
  // result.coverage when sub-models are used. See GenerateResult docs.
  result.coverage = coverage.CoverageRatio();
  for (const auto& eng : sub_engines) {
    result.coverage = std::min(result.coverage, eng.CoverageRatio());
  }
  result.stats.total_tuples = coverage.TotalTuples();
  for (const auto& eng : sub_engines) {
    result.stats.total_tuples += eng.TotalTuples();
  }
  result.stats.covered_tuples = coverage.CoveredCount();
  for (const auto& eng : sub_engines) {
    result.stats.covered_tuples += eng.CoveredCount();
  }
  result.stats.test_count = static_cast<uint32_t>(result.tests.size());

  // Class coverage belongs to the generation result itself. Annotating here
  // uses the exact parsed constraints and effective (boundary-expanded)
  // parameters, keeping every public wrapper on the same semantics.
  validator::AnnotateClassCoverage(result, opts.parameters, opts.strength, constraints);
  if (!result.error.ok()) return result;

  // Reuse the global engine only after all positive metrics and diagnostics
  // have been captured. This keeps the peak tuple bitmap budget unchanged.
  if (has_invalid) {
    model::NegativeCoverage negative_coverage;
    model::Error negative_error = GenerateNegativeTests(
        opts.parameters, constraints, coverage, opts.max_tests, result.tests.size(),
        result.negative_tests, negative_coverage, result.warnings);
    result.negative_coverage = negative_coverage;
    result.stats.test_count =
        static_cast<uint32_t>(result.tests.size() + result.negative_tests.size());
    if (!negative_error.ok()) {
      result.error = negative_error;
      result.warnings.push_back(result.error.message + ": " + result.error.detail);
    }
  }

  return result;
}

model::GenerateResult Generate(const GenerateOptions& options) { return GenerateImpl(options, 0); }

model::GenerateResult Extend(const std::vector<model::TestCase>& existing,
                             const GenerateOptions& options, ExtendMode mode) {
  GenerateOptions opts = options;

  // Only kStrict is supported today: existing tests are seeded verbatim and kept
  // as-is, with new tests appended to improve coverage. Switch explicitly so a
  // future mode cannot be silently treated as strict.
  switch (mode) {
    case ExtendMode::kStrict:
      if (opts.max_tests > 0 && existing.size() > static_cast<size_t>(opts.max_tests)) {
        model::GenerateResult result;
        result.error = {model::Error::Code::kInvalidInput,
                        "maxTests cannot be smaller than the existing test count",
                        "maxTests=" + std::to_string(opts.max_tests) +
                            ", existing=" + std::to_string(existing.size())};
        result.warnings.push_back(result.error.message + ": " + result.error.detail);
        return result;
      }
      opts.seeds.insert(opts.seeds.begin(), existing.begin(), existing.end());
      break;
  }

  return GenerateImpl(opts, existing.size());
}

ModelStats EstimateModel(const GenerateOptions& options) {
  ModelStats stats;
  stats.error = model::ValidateGenerateOptions(options);
  if (!stats.error.ok()) return stats;

  // Apply boundary expansion for estimation.
  GenerateOptions opts = options;
  ApplyBoundaryExpansion(opts);

  // Estimation intentionally remains a raw tuple estimate, but it is still a
  // model preflight API: syntax/reference errors must match generate.
  for (const auto& expression : opts.constraint_expressions) {
    auto parsed = model::ParseConstraint(expression, opts.parameters);
    if (!parsed.error.ok()) {
      stats.error = model::AnnotateConstraintError(expression, parsed.error);
      return stats;
    }
  }

  stats.parameter_count = static_cast<uint32_t>(opts.parameters.size());
  stats.strength = opts.strength;
  stats.sub_model_count = static_cast<uint32_t>(opts.sub_models.size());
  stats.constraint_count = static_cast<uint32_t>(opts.constraint_expressions.size());

  uint32_t max_values = 0;
  for (const auto& p : opts.parameters) {
    stats.total_values += p.size();
    if (p.size() > max_values) {
      max_values = p.size();
    }
    ModelStats::ParamDetail detail;
    detail.name = p.name;
    detail.value_count = p.size();
    detail.invalid_count = p.invalid_count();
    stats.parameters.push_back(std::move(detail));
  }

  // Compute the raw global + sub-model tuple upper bound using the same engine
  // definitions and combined allocation budget as generation. Constraints are
  // intentionally not subtracted because estimation does not solve the model.
  auto [coverage, err] = CoverageEngine::Create(opts.parameters, opts.strength);
  if (!err.ok()) {
    stats.error = err;
    return stats;
  }
  stats.total_tuples = coverage.TotalTuples();
  for (const auto& sm : opts.sub_models) {
    auto [indices, resolve_error] = ResolveParamNames(sm.parameter_names, opts.parameters);
    if (!resolve_error.empty()) {
      stats.error = {model::Error::Code::kInvalidInput, resolve_error, ""};
      return stats;
    }
    auto [sub_coverage, sub_error] = CoverageEngine::Create(opts.parameters, indices, sm.strength);
    if (!sub_error.ok()) {
      stats.error = sub_error;
      return stats;
    }
    if (sub_coverage.TotalTuples() > CoverageEngine::kMaxTuples - stats.total_tuples) {
      stats.error = {model::Error::Code::kTupleExplosion,
                     "Combined global and sub-model tuple count exceeds safe limit",
                     "limit=" + std::to_string(CoverageEngine::kMaxTuples)};
      return stats;
    }
    stats.total_tuples += sub_coverage.TotalTuples();
  }

  // Estimate test count: upper bound is max_values^strength.
  // For better estimate, use max_values^strength when param_count > strength,
  // otherwise it is the product of all value counts.
  if (stats.parameter_count == 0) {
    stats.estimated_tests = 0;
  } else if (stats.parameter_count <= stats.strength) {
    uint64_t product = 1;
    for (const auto& p : opts.parameters) {
      product *= p.size();
      if (product > UINT32_MAX) break;
    }
    stats.estimated_tests =
        static_cast<uint32_t>(std::min(product, static_cast<uint64_t>(UINT32_MAX)));
  } else {
    uint64_t estimate = 1;
    for (uint32_t i = 0; i < stats.strength; ++i) {
      estimate *= max_values;
      if (estimate > UINT32_MAX) break;
    }
    // Refine with log factor: roughly max_v^t * ceil(log2(n))
    uint32_t log_factor =
        static_cast<uint32_t>(std::ceil(std::log2(static_cast<double>(stats.parameter_count))));
    if (log_factor < 1) log_factor = 1;
    estimate *= log_factor;
    // Cap at total_tuples (can't need more tests than tuples).
    if (stats.total_tuples > 0 && estimate > stats.total_tuples) {
      estimate = stats.total_tuples;
    }
    stats.estimated_tests =
        static_cast<uint32_t>(std::min(estimate, static_cast<uint64_t>(UINT32_MAX)));
  }

  return stats;
}

}  // namespace core
}  // namespace coverwise
