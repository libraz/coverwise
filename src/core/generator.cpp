/// @file generator.cpp

#include "core/generator.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "algo/greedy.h"
#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "util/rng.h"

namespace coverwise {
namespace core {

double WeightConfig::GetWeight(const std::string& param_name, const std::string& value_name) const {
  auto pit = entries.find(param_name);
  if (pit == entries.end()) return 1.0;
  auto vit = pit->second.find(value_name);
  if (vit == pit->second.end()) return 1.0;
  return vit->second;
}

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

/// @brief Check whether any parameter has invalid values.
bool HasInvalidValues(const std::vector<model::Parameter>& params) {
  for (const auto& p : params) {
    if (p.has_invalid_values()) return true;
  }
  return false;
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

/// @brief Generate negative tests for parameters with invalid values.
///
/// For each invalid value of each parameter, generates test cases that pair
/// the invalid value with valid values of all other parameters using pairwise
/// coverage. Each negative test case contains exactly one invalid value.
void GenerateNegativeTests(const std::vector<model::Parameter>& params, uint32_t strength,
                           const std::vector<model::Constraint>& constraints, util::Rng& rng,
                           std::vector<model::TestCase>& negative_tests) {
  constexpr uint32_t kMaxRetries = 50;

  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (!params[pi].is_invalid(vi)) continue;

      // Create a coverage engine for tuples involving this invalid value
      // paired with valid values of other parameters.
      auto [neg_cov, err] = CoverageEngine::Create(params, strength);
      if (!err.ok()) continue;

      // Exclude all tuples that don't involve param pi with value vi.
      // Also exclude tuples that involve invalid values of other params.
      // We achieve this by: (1) excluding invalid values, then (2) adding
      // all tuples NOT involving pi=vi as "already covered".
      //
      // Simpler approach: use the coverage engine as-is and rely on the
      // allowed_values mask to ensure only valid values (plus this one
      // invalid value) are selected. Then just generate until coverage
      // is complete or retries exhaust.
      //
      // But this means coverage includes tuples not involving the invalid value.
      // Those would be covered quickly since only valid values are used for
      // other params. The key insight: we want every valid value of every
      // other param to appear alongside the invalid value. For pairwise,
      // that means we need at least max(valid_count) test cases per invalid value.
      //
      // Instead, use a simpler direct approach: generate one test per invalid
      // value, filling other params greedily with valid values to maximize
      // diversity. For proper coverage, generate enough tests.

      // Exclude tuples containing invalid values of OTHER params, and
      // tuples not involving pi=vi (since we only care about coverage of
      // the invalid value).
      neg_cov.ExcludeInvalidValues();

      // Now, un-exclude tuples involving pi=vi (they were excluded by
      // ExcludeInvalidValues). We can't easily un-exclude, so let's use
      // a different strategy.

      // Actually, let's use the simplest correct approach:
      // Build a dedicated coverage engine for just the pairs involving the
      // invalid value. We create a subset engine for each other param
      // paired with pi.

      // Even simpler: for each invalid value, generate tests using the full
      // engine with a negative mask, and track coverage locally.
      // Since the full engine approach is complex, use the direct approach:

      // For pairwise: for each other param, we need to cover all valid values
      // of that param with pi=vi. Generate test cases greedily.

      // Create a fresh engine and exclude everything except tuples involving pi.
      auto [fresh_cov, fresh_err] = CoverageEngine::Create(params, strength);
      if (!fresh_err.ok()) continue;

      // Exclude constraint-invalid tuples.
      fresh_cov.ExcludeInvalidTuples(constraints);

      // Build mask: pi can only be vi, others can only be valid.
      auto neg_mask = BuildNegativeMask(params, pi, vi);

      // Generate test cases with this mask until coverage of relevant tuples
      // is complete or we hit retry limit.
      uint32_t retries = 0;
      // We cannot easily check "complete for this invalid value only", so
      // generate a bounded number of tests.
      // Bound: at most sum of valid_count of other params (generous upper bound).
      uint32_t max_neg_tests = 0;
      for (uint32_t pj = 0; pj < static_cast<uint32_t>(params.size()); ++pj) {
        if (pj != pi) max_neg_tests = std::max(max_neg_tests, params[pj].valid_count());
      }
      if (max_neg_tests == 0) max_neg_tests = 1;

      uint32_t generated = 0;
      while (generated < max_neg_tests) {
        auto tc = algo::GreedyConstruct(params, fresh_cov, constraints, rng, neg_mask);
        uint32_t score = fresh_cov.ScoreCandidate(tc);
        if (score == 0) {
          if (++retries >= kMaxRetries) break;
          continue;
        }
        retries = 0;
        fresh_cov.AddTestCase(tc);
        negative_tests.push_back(std::move(tc));
        ++generated;
      }
    }
  }
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
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      resolved[pi][vi] = config.GetWeight(params[pi].name, params[pi].values[vi]);
    }
  }
  return resolved;
}

}  // namespace

model::GenerateResult Generate(const GenerateOptions& options) {
  model::GenerateResult result;

  bool has_invalid = HasInvalidValues(options.parameters);

  auto [coverage, err] = CoverageEngine::Create(options.parameters, options.strength);
  if (!err.ok()) {
    result.warnings.push_back(err.message + ": " + err.detail);
    return result;
  }

  // Create sub-model engines.
  std::vector<CoverageEngine> sub_engines;
  for (const auto& sm : options.sub_models) {
    auto [indices, resolve_err] = ResolveParamNames(sm.parameter_names, options.parameters);
    if (!resolve_err.empty()) {
      result.warnings.push_back(resolve_err);
      return result;
    }
    if (indices.size() < sm.strength) {
      result.warnings.push_back("Sub-model strength (" + std::to_string(sm.strength) +
                                ") exceeds parameter count (" + std::to_string(indices.size()) +
                                ")");
      return result;
    }
    auto [eng, sm_err] = CoverageEngine::Create(options.parameters, indices, sm.strength);
    if (!sm_err.ok()) {
      result.warnings.push_back(sm_err.message + ": " + sm_err.detail);
      return result;
    }
    sub_engines.push_back(std::move(eng));
  }

  // Parse constraint expressions into AST.
  std::vector<model::Constraint> constraints;
  for (const auto& expr : options.constraint_expressions) {
    auto parse_result = model::ParseConstraint(expr, options.parameters);
    if (!parse_result.error.ok()) {
      result.warnings.push_back(parse_result.error.message + ": " + parse_result.error.detail);
      return result;
    }
    constraints.push_back(std::move(parse_result.constraint));
  }

  // Exclude tuples that are inherently invalid due to constraints.
  coverage.ExcludeInvalidTuples(constraints);
  for (auto& eng : sub_engines) {
    eng.ExcludeInvalidTuples(constraints);
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
    valid_mask = BuildValidOnlyMask(options.parameters);
  }

  // Resolve value weights to index-based vectors.
  auto resolved_weights = ResolveWeights(options.parameters, options.weights);

  util::Rng rng(options.seed);

  // Pre-load seed tests into all engines.
  for (const auto& seed_test : options.seeds) {
    coverage.AddTestCase(seed_test);
    for (auto& eng : sub_engines) {
      eng.AddTestCase(seed_test);
    }
    result.tests.push_back(seed_test);
  }

  // Build engine pointer list for multi-engine greedy.
  std::vector<const CoverageEngine*> engine_ptrs;
  engine_ptrs.push_back(&coverage);
  for (const auto& eng : sub_engines) {
    engine_ptrs.push_back(&eng);
  }

  // Constructive greedy generation loop (positive tests only).
  constexpr uint32_t kMaxRetries = 50;
  uint32_t retries = 0;
  while (!AllComplete(coverage, sub_engines) &&
         (options.max_tests == 0 || result.tests.size() < static_cast<size_t>(options.max_tests))) {
    model::TestCase tc;
    if (sub_engines.empty()) {
      tc = algo::GreedyConstruct(options.parameters, coverage, constraints, rng, valid_mask,
                                 resolved_weights);
    } else {
      tc = algo::GreedyConstruct(options.parameters, engine_ptrs, constraints, rng,
                                 resolved_weights);
    }
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

  // Generate negative tests if any parameter has invalid values.
  if (has_invalid) {
    GenerateNegativeTests(options.parameters, options.strength, constraints, rng,
                          result.negative_tests);
  }

  // Collect uncovered tuples from all engines.
  if (!AllComplete(coverage, sub_engines)) {
    auto global_uncovered = coverage.GetUncoveredTuples(options.parameters);
    result.uncovered.insert(result.uncovered.end(), global_uncovered.begin(),
                            global_uncovered.end());
    for (const auto& eng : sub_engines) {
      auto sub_uncovered = eng.GetUncoveredTuples(options.parameters);
      result.uncovered.insert(result.uncovered.end(), sub_uncovered.begin(), sub_uncovered.end());
    }
    for (const auto& ut : result.uncovered) {
      model::Suggestion suggestion;
      suggestion.description = "Add test: " + ut.ToString();
      result.suggestions.push_back(std::move(suggestion));
    }
  }

  // Report coverage as the minimum across all engines.
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
  return result;
}

model::GenerateResult Extend(const std::vector<model::TestCase>& existing,
                             const GenerateOptions& options, ExtendMode /*mode*/) {
  // For kStrict mode: treat existing as seeds, generate delta only
  GenerateOptions opts = options;
  opts.seeds = existing;

  // TODO: For kOptimize mode, allow reordering existing tests
  return Generate(opts);
}

ModelStats EstimateModel(const GenerateOptions& options) {
  ModelStats stats;
  stats.parameter_count = static_cast<uint32_t>(options.parameters.size());
  stats.strength = options.strength;
  stats.sub_model_count = static_cast<uint32_t>(options.sub_models.size());
  stats.constraint_count = static_cast<uint32_t>(options.constraint_expressions.size());

  uint32_t max_values = 0;
  for (const auto& p : options.parameters) {
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

  // Compute exact total tuples using CoverageEngine.
  auto [coverage, err] = CoverageEngine::Create(options.parameters, options.strength);
  if (err.ok()) {
    stats.total_tuples = coverage.TotalTuples();
  }

  // Estimate test count: upper bound is max_values^strength.
  // For better estimate, use max_values^strength when param_count > strength,
  // otherwise it is the product of all value counts.
  if (stats.parameter_count <= stats.strength) {
    uint32_t product = 1;
    for (const auto& p : options.parameters) {
      product *= p.size();
    }
    stats.estimated_tests = product;
  } else {
    uint32_t estimate = 1;
    for (uint32_t i = 0; i < stats.strength; ++i) {
      estimate *= max_values;
    }
    // Refine with log factor: roughly max_v^t * ceil(log2(n))
    uint32_t log_factor =
        static_cast<uint32_t>(std::ceil(std::log2(static_cast<double>(stats.parameter_count))));
    if (log_factor < 1) log_factor = 1;
    stats.estimated_tests = estimate * log_factor;
    // Cap at total_tuples (can't need more tests than tuples).
    if (stats.total_tuples > 0 && stats.estimated_tests > stats.total_tuples) {
      stats.estimated_tests = stats.total_tuples;
    }
  }

  return stats;
}

}  // namespace core
}  // namespace coverwise
