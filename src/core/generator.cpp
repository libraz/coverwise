/// @file generator.cpp

#include "core/generator.h"

#include <vector>

#include "algo/greedy.h"
#include "core/coverage_engine.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "util/rng.h"

namespace coverwise {
namespace core {

model::GenerateResult Generate(const GenerateOptions& options) {
  model::GenerateResult result;

  auto [coverage, err] = CoverageEngine::Create(options.parameters, options.strength);
  if (!err.ok()) {
    result.warnings.push_back(err.message + ": " + err.detail);
    return result;
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

  util::Rng rng(options.seed);

  // Pre-load seed tests
  for (const auto& seed_test : options.seeds) {
    coverage.AddTestCase(seed_test);
    result.tests.push_back(seed_test);
  }

  // Constructive greedy generation loop.
  // The greedy algorithm is randomized, so a single attempt may produce a
  // redundant test case (score=0). Retry up to kMaxRetries times before
  // concluding that no progress is possible.
  constexpr uint32_t kMaxRetries = 50;
  uint32_t retries = 0;
  while (!coverage.IsComplete() &&
         (options.max_tests == 0 || result.tests.size() < static_cast<size_t>(options.max_tests))) {
    model::TestCase tc = algo::GreedyConstruct(options.parameters, coverage, constraints, rng);
    uint32_t score = coverage.ScoreCandidate(tc);
    if (score == 0) {
      if (++retries >= kMaxRetries) break;
      continue;
    }
    retries = 0;
    coverage.AddTestCase(tc);
    result.tests.push_back(std::move(tc));
  }

  // Collect uncovered tuples and suggestions if coverage is incomplete.
  if (!coverage.IsComplete()) {
    result.uncovered = coverage.GetUncoveredTuples(options.parameters);
    for (const auto& ut : result.uncovered) {
      model::Suggestion suggestion;
      suggestion.description = "Add test: " + ut.ToString();
      result.suggestions.push_back(std::move(suggestion));
    }
  }

  result.coverage = coverage.CoverageRatio();
  result.stats.total_tuples = coverage.TotalTuples();
  result.stats.covered_tuples = coverage.CoveredCount();
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

}  // namespace core
}  // namespace coverwise
