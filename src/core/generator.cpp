/// @file generator.cpp

#include "core/generator.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "algo/greedy.h"
#include "core/constraint_solver.h"
#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/options_validation.h"
#include "model/surface_error.h"
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

/// @brief The tuple space one coverage engine enumerates.
///
/// A tuple is identified by its parameter set and value tuple, not by the
/// engine that reports it, so two engines describe the same interaction exactly
/// when their shapes overlap. Kept beside the engines because the engine itself
/// does not expose its subset or strength.
struct EngineShape {
  std::vector<uint32_t> params;  ///< Global parameter indices, ascending.
  uint32_t strength = 0;
};

/// @brief Whether two engines can enumerate a common tuple.
///
/// Tuple identity includes the parameter set, so engines of different strengths
/// never collide, and engines sharing fewer than `strength` parameters have no
/// parameter combination in common either.
bool ShapesOverlap(const EngineShape& a, const EngineShape& b) {
  if (a.strength != b.strength) return false;
  std::vector<uint32_t> shared;
  std::set_intersection(a.params.begin(), a.params.end(), b.params.begin(), b.params.end(),
                        std::back_inserter(shared));
  return shared.size() >= a.strength;
}

/// @brief Whether @p engine still counts the tuple @p indices as uncovered.
///
/// Scoring one (parameter, value) pair against a partial assignment that fixes
/// the tuple's remaining pairs isolates a single parameter combination: no other
/// combination containing the scored parameter is fully assigned. The score is
/// therefore 1 when that combination's value tuple is still uncovered here, and
/// 0 when it is covered, excluded, or outside the engine's parameter subset.
/// Only meaningful for an engine whose strength equals the tuple size, which
/// ShapesOverlap() establishes before this is called.
bool EngineNeedsTuple(const CoverageEngine& engine,
                      const std::vector<std::pair<uint32_t, uint32_t>>& indices,
                      size_t param_count) {
  if (indices.empty()) return false;
  model::TestCase partial;
  partial.values.assign(param_count, model::kUnassigned);
  for (size_t k = 1; k < indices.size(); ++k) {
    partial.values[indices[k].first] = indices[k].second;
  }
  return engine.ScoreValue(partial, indices[0].first, indices[0].second) == 1;
}

/// @brief Check if all engines are complete.
bool AllComplete(const CoverageEngine& global, const std::vector<CoverageEngine>& sub_engines) {
  if (!global.IsComplete()) return false;
  for (const auto& eng : sub_engines) {
    if (!eng.IsComplete()) return false;
  }
  return true;
}

/// @brief Scratch state held for the length of one generation pass.
///
/// Everything a hot loop would otherwise rebuild per iteration is owned here and
/// borrowed by the loop: the greedy construction buffers and the feasibility
/// solver's parameter order. The order depends only on the allowed-value mask,
/// so it is rebuilt exactly when that mask changes — once for the positive
/// phase, once per invalid value in the negative phase — and never per witness.
struct GenerationScratch {
  algo::GreedyScratch greedy;
  SolveParameterOrder solve_order;
};

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
      // A row the caller recorded is described back to them in their own terms.
      // The index here is kUnassigned whenever the row drifted from the model,
      // and printing that sentinel tells the caller nothing about which part of
      // what they submitted no longer fits.
      if (pi < seed.unresolved.size() && !seed.unresolved[pi].empty()) {
        return "value '" + seed.unresolved[pi] + "' is not declared by parameter " +
               params[pi].name;
      }
      if (vi == model::kUnassigned) {
        return "no value recorded for parameter " + params[pi].name;
      }
      // Reached only through the embedding API, which supplies indices itself,
      // so the index is the caller's own input rather than an internal one.
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
                                   size_t positive_test_count, GenerationScratch& scratch,
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

      // The mask is fixed for the whole inner loop, so the solver order derived
      // from it is built once here rather than once per witness.
      scratch.solve_order = BuildAllowedSolveParameterOrder(params, neg_mask);

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
        if (!CompleteAssignment(params, constraints, neg_mask, witness, &budget,
                                &scratch.solve_order)) {
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
    result.warnings.push_back(model::SurfaceError(result.error).text());
    return result;
  }

  // Apply boundary value expansion to parameters that have boundary configs.
  GenerateOptions opts = options;
  ApplyBoundaryExpansion(opts);
  result.parameters = opts.parameters;
  auto expanded_param_error = model::ValidateParameters(opts.parameters);
  if (!expanded_param_error.ok()) {
    result.error = expanded_param_error;
    result.warnings.push_back(model::SurfaceError(result.error).text());
    return result;
  }

  bool has_invalid = model::HasInvalidValues(opts.parameters);

  // Every engine over this model reads the same parameters, so they are shared
  // rather than copied once per engine.
  auto shared_params = CoverageEngine::ShareParameters(opts.parameters);
  auto coverage_result = CoverageEngine::CreateShared(shared_params, opts.strength);
  if (!coverage_result.second.ok()) {
    result.warnings.push_back(model::SurfaceError(coverage_result.second).text());
    result.error = coverage_result.second;
    return result;
  }
  auto coverage = std::move(coverage_result.first);
  uint64_t allocated_tuples = coverage.TotalTuples();

  // Create sub-model engines. Their tuple spaces are recorded alongside so the
  // uncovered diagnostics can tell overlapping engines apart from disjoint ones.
  std::vector<CoverageEngine> sub_engines;
  std::vector<EngineShape> sub_shapes;
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
    auto [eng, sm_err] = CoverageEngine::CreateShared(shared_params, indices, sm.strength);
    if (!sm_err.ok()) {
      result.warnings.push_back(model::SurfaceError(sm_err).text());
      result.error = sm_err;
      return result;
    }
    if (eng.TotalTuples() > CoverageEngine::kMaxTuples - allocated_tuples) {
      result.error = {model::Error::Code::kTupleExplosion,
                      "Combined global and sub-model tuple count exceeds safe limit",
                      "limit=" + std::to_string(CoverageEngine::kMaxTuples)};
      result.warnings.push_back(model::SurfaceError(result.error).text());
      return result;
    }
    allocated_tuples += eng.TotalTuples();
    sub_engines.push_back(std::move(eng));
    sub_shapes.push_back(EngineShape{indices, sm.strength});
  }

  // Parse constraint expressions into AST.
  std::vector<model::Constraint> constraints;
  for (const auto& expr : opts.constraint_expressions) {
    auto parse_result = model::ParseConstraint(expr, opts.parameters);
    if (!parse_result.error.ok()) {
      model::Error err = model::AnnotateConstraintError(expr, parse_result.error);
      result.warnings.push_back(model::SurfaceError(err).text());
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
      result.warnings.push_back(model::SurfaceError(result.error).text());
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
    result.warnings.push_back(model::SurfaceError(result.error).text());
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

  // Scratch borrowed by the construction and completion loops below.
  GenerationScratch scratch;
  scratch.greedy.Reserve(opts.parameters);

  // Scoring callbacks are passed to GreedyConstruct as template arguments, so
  // the per-parameter call is a direct call rather than a type-erased one.
  auto simple_score_values = [&](const model::TestCase& partial, uint32_t pi, uint32_t* scores) {
    coverage.AddValueScores(partial, pi, scores);
  };
  auto combined_score_values = [&](const model::TestCase& partial, uint32_t pi, uint32_t* scores) {
    coverage.AddValueScores(partial, pi, scores);
    for (const auto& eng : sub_engines) {
      eng.AddValueScores(partial, pi, scores);
    }
  };

  // Constructive greedy generation loop (positive tests only).
  constexpr uint32_t kMaxRetries = 50;
  uint32_t retries = 0;
  while (!AllComplete(coverage, sub_engines) &&
         (opts.max_tests == 0 || result.tests.size() < static_cast<size_t>(opts.max_tests))) {
    auto built = sub_engines.empty()
                     ? algo::GreedyConstruct(opts.parameters, simple_score_values, constraints, rng,
                                             scratch.greedy, valid_mask, resolved_weights)
                     : algo::GreedyConstruct(opts.parameters, combined_score_values, constraints,
                                             rng, scratch.greedy, valid_mask, resolved_weights);
    // A failed construction (no constraint-satisfying value for some parameter)
    // is treated like a zero-score candidate: retry with a different shuffle.
    if (!built) {
      if (++retries >= kMaxRetries) break;
      continue;
    }
    // Construction already summed the gain of every combination it completed,
    // and coverage does not change while a test case is being built, so the
    // candidate needs no second full scan of the coverage bitmaps.
    if (built->score == 0) {
      if (++retries >= kMaxRetries) break;
      continue;
    }
    auto& tc = built->test_case;
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
  // The mask this order derives from is fixed for the whole positive phase, so
  // it is built once instead of once per uncovered tuple.
  scratch.solve_order = has_invalid ? BuildAllowedSolveParameterOrder(opts.parameters, valid_mask)
                                    : BuildValidSolveParameterOrder(opts.parameters);
  auto complete_partial = [&](model::TestCase& witness) {
    SolveBudget budget;
    bool ok = has_invalid ? CompleteAssignment(opts.parameters, constraints, valid_mask, witness,
                                               &budget, &scratch.solve_order)
                          : CompleteValidAssignment(opts.parameters, constraints, witness, &budget,
                                                    &scratch.solve_order);
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
    result.warnings.push_back(model::SurfaceError(result.error).text());
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

  // Collect uncovered tuples from all engines. A sub-model can enumerate the
  // same interaction as the global model or as another sub-model, so both the
  // total and the diagnostic list describe the union over engines: every
  // distinct tuple is counted once, listed once, and suggested once.
  if (!AllComplete(coverage, sub_engines)) {
    EngineShape global_shape;
    global_shape.params.reserve(opts.parameters.size());
    for (uint32_t pi = 0; pi < static_cast<uint32_t>(opts.parameters.size()); ++pi) {
      global_shape.params.push_back(pi);
    }
    global_shape.strength = opts.strength;

    // The global engine is counted first, so every tuple it needs is new. An
    // engine whose tuple space is disjoint from all earlier ones contributes its
    // whole shortfall; only an overlap has to be resolved tuple by tuple.
    result.uncovered_count = coverage.TotalTuples() - coverage.CoveredCount();
    for (size_t i = 0; i < sub_engines.size(); ++i) {
      std::vector<const CoverageEngine*> earlier;
      if (ShapesOverlap(sub_shapes[i], global_shape)) earlier.push_back(&coverage);
      for (size_t j = 0; j < i; ++j) {
        if (ShapesOverlap(sub_shapes[i], sub_shapes[j])) earlier.push_back(&sub_engines[j]);
      }
      uint32_t shortfall = sub_engines[i].TotalTuples() - sub_engines[i].CoveredCount();
      if (earlier.empty()) {
        result.uncovered_count += shortfall;
        continue;
      }
      for (const auto& ut : sub_engines[i].GetUncoveredTuples(opts.parameters, shortfall)) {
        bool already_counted = false;
        for (const CoverageEngine* eng : earlier) {
          if (EngineNeedsTuple(*eng, ut.indices, opts.parameters.size())) {
            already_counted = true;
            break;
          }
        }
        if (!already_counted) ++result.uncovered_count;
      }
    }

    // Fill the diagnostic budget with distinct tuples: each engine is asked for
    // a full budget's worth rather than the remaining slots, so tuples already
    // listed by an earlier engine do not shrink the report.
    std::set<std::vector<std::pair<uint32_t, uint32_t>>> listed;
    auto append_distinct = [&](std::vector<model::UncoveredTuple> tuples) {
      for (auto& ut : tuples) {
        if (result.uncovered.size() >= CoverageEngine::kMaxDiagnosticTuples) return;
        if (!listed.insert(ut.indices).second) continue;
        result.uncovered.push_back(std::move(ut));
      }
    };
    append_distinct(coverage.GetUncoveredTuples(opts.parameters));
    for (const auto& eng : sub_engines) {
      if (result.uncovered.size() >= CoverageEngine::kMaxDiagnosticTuples) break;
      append_distinct(
          eng.GetUncoveredTuples(opts.parameters, CoverageEngine::kMaxDiagnosticTuples));
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
        opts.parameters, constraints, coverage, opts.max_tests, result.tests.size(), scratch,
        result.negative_tests, negative_coverage, result.warnings);
    // A pass that stopped on an exhausted search budget never reached the
    // omitted/ratio finalization, so its counters do not describe a whole tuple
    // universe. Leave the field unset instead of publishing a self-contradictory
    // report, matching how class coverage reports its own failures.
    if (negative_error.ok()) {
      result.negative_coverage = negative_coverage;
    }
    result.stats.test_count =
        static_cast<uint32_t>(result.tests.size() + result.negative_tests.size());
    if (!negative_error.ok()) {
      result.error = negative_error;
      result.warnings.push_back(model::SurfaceError(result.error).text());
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
        result.warnings.push_back(model::SurfaceError(result.error).text());
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
  auto shared_params = CoverageEngine::ShareParameters(opts.parameters);
  auto [coverage, err] = CoverageEngine::CreateShared(shared_params, opts.strength);
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
    auto [sub_coverage, sub_error] =
        CoverageEngine::CreateShared(shared_params, indices, sm.strength);
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

  // Estimate the suite size. When every parameter fits in one tuple the exact
  // product of the value counts is the answer; otherwise the heuristic is
  // max_values^strength scaled by a log factor over the parameter count. The
  // scaled form is a sizing hint only -- a generated suite can fall on either
  // side of it.
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
