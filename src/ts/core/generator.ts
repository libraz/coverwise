/// @file generator.ts
/// @brief Main test generation orchestrator.

import { greedyConstruct, type ScoreFn } from '../algo/greedy.js';
import { expandBoundaryValues } from '../model/boundary.js';
import type { ConstraintNode } from '../model/constraint-ast.js';
import { parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode } from '../model/error.js';
import {
  createModelStats,
  ExtendMode,
  type GenerateOptions,
  getWeight,
  isWeightConfigEmpty,
  type ModelStats,
  type WeightConfig,
} from '../model/generate-options.js';
import { hasInvalidValues, Parameter } from '../model/parameter.js';
import {
  createGenerateResult,
  type GenerateResult,
  type Suggestion,
  type TestCase,
  uncoveredTupleToString,
} from '../model/test-case.js';
import { Rng } from '../util/rng.js';
import { CoverageEngine } from './coverage-engine.js';

/// Resolve parameter names to sorted indices.
/// Returns indices array and error message (empty string on success).
function resolveParamNames(
  names: string[],
  params: Parameter[],
): { indices: number[]; error: string } {
  const indices: number[] = [];
  for (const name of names) {
    let found = false;
    for (let i = 0; i < params.length; ++i) {
      if (params[i].name === name) {
        indices.push(i);
        found = true;
        break;
      }
    }
    if (!found) {
      return { indices: [], error: `Unknown parameter in sub-model: ${name}` };
    }
  }
  // Sort for consistent combination generation.
  indices.sort((a, b) => a - b);
  return { indices, error: '' };
}

/// Check if all engines are complete.
function allComplete(global: CoverageEngine, subEngines: CoverageEngine[]): boolean {
  if (!global.isComplete) {
    return false;
  }
  for (const eng of subEngines) {
    if (!eng.isComplete) {
      return false;
    }
  }
  return true;
}

/// Sum scoreCandidate across all engines.
function totalScore(global: CoverageEngine, subEngines: CoverageEngine[], tc: TestCase): number {
  let score = global.scoreCandidate(tc);
  for (const eng of subEngines) {
    score += eng.scoreCandidate(tc);
  }
  return score;
}

/// Build an allowedValues mask that only permits valid values.
function buildValidOnlyMask(params: Parameter[]): boolean[][] {
  const mask: boolean[][] = new Array(params.length);
  for (let pi = 0; pi < params.length; ++pi) {
    mask[pi] = new Array<boolean>(params[pi].size);
    for (let vi = 0; vi < params[pi].size; ++vi) {
      mask[pi][vi] = !params[pi].isInvalid(vi);
    }
  }
  return mask;
}

/// Build an allowedValues mask for negative test generation.
///
/// The fixed parameter is allowed only at the given invalid value index.
/// All other parameters are allowed only at their valid values.
function buildNegativeMask(
  params: Parameter[],
  fixedParam: number,
  fixedValue: number,
): boolean[][] {
  const mask: boolean[][] = new Array(params.length);
  for (let pi = 0; pi < params.length; ++pi) {
    mask[pi] = new Array<boolean>(params[pi].size).fill(false);
    if (pi === fixedParam) {
      mask[pi][fixedValue] = true;
    } else {
      for (let vi = 0; vi < params[pi].size; ++vi) {
        if (!params[pi].isInvalid(vi)) {
          mask[pi][vi] = true;
        }
      }
    }
  }
  return mask;
}

/// Generate negative tests for parameters with invalid values.
///
/// For each invalid value of each parameter, generates test cases that pair
/// the invalid value with valid values of all other parameters using pairwise
/// coverage. Each negative test case contains exactly one invalid value.
function generateNegativeTests(
  params: Parameter[],
  strength: number,
  constraints: ConstraintNode[],
  rng: Rng,
  negativeTests: TestCase[],
): void {
  const kMaxRetries = 50;

  for (let pi = 0; pi < params.length; ++pi) {
    for (let vi = 0; vi < params[pi].size; ++vi) {
      if (!params[pi].isInvalid(vi)) {
        continue;
      }

      // Create a coverage engine for generating tests that pair this invalid
      // value with valid values of all other parameters.
      const freshResult = CoverageEngine.create(params, strength);
      if (freshResult.error.code !== ErrorCode.Ok) {
        continue;
      }
      const freshCov = freshResult.engine;

      // Exclude constraint-invalid tuples.
      freshCov.excludeInvalidTuples(constraints);

      // Build mask: pi can only be vi, others can only be valid.
      const negMask = buildNegativeMask(params, pi, vi);

      // Generate test cases with this mask until coverage of relevant tuples
      // is complete or we hit retry limit.
      let retries = 0;
      // Bound: at most max valid_count of other params (generous upper bound).
      let maxNegTests = 0;
      for (let pj = 0; pj < params.length; ++pj) {
        if (pj !== pi) {
          maxNegTests = Math.max(maxNegTests, params[pj].validCount);
        }
      }
      if (maxNegTests === 0) {
        maxNegTests = 1;
      }

      let generated = 0;
      while (generated < maxNegTests) {
        const negScoreFn: ScoreFn = (partial, paramIdx, valIdx) => {
          return freshCov.scoreValue(partial, paramIdx, valIdx);
        };
        const tc = greedyConstruct(params, negScoreFn, constraints, rng, negMask);
        const score = freshCov.scoreCandidate(tc);
        if (score === 0) {
          if (++retries >= kMaxRetries) {
            break;
          }
          continue;
        }
        retries = 0;
        freshCov.addTestCase(tc);
        negativeTests.push(tc);
        ++generated;
      }
    }
  }
}

/// Resolve string-based WeightConfig to index-based weight vectors.
/// Returns weights[paramIdx][valueIdx] = weight (default 1.0).
/// Empty array if no weights are configured.
function resolveWeights(params: Parameter[], config: WeightConfig): number[][] {
  if (isWeightConfigEmpty(config)) {
    return [];
  }
  const resolved: number[][] = new Array(params.length);
  for (let pi = 0; pi < params.length; ++pi) {
    resolved[pi] = new Array<number>(params[pi].size);
    for (let vi = 0; vi < params[pi].size; ++vi) {
      resolved[pi][vi] = getWeight(config, params[pi].name, params[pi].values[vi]);
    }
  }
  return resolved;
}

/// Apply boundary value expansion to parameters with boundary configs.
function applyBoundaryExpansion(opts: GenerateOptions): Parameter[] {
  const params: Parameter[] = [];
  for (const p of opts.parameters) {
    const param = p.invalid
      ? new Parameter(p.name, p.values, p.invalid)
      : new Parameter(p.name, p.values);
    const bc = opts.boundaryConfigs[p.name];
    if (bc) {
      params.push(expandBoundaryValues(param, bc));
    } else {
      params.push(param);
    }
  }
  return params;
}

/// Generate a covering array for the given options.
/// @returns The generated test suite with coverage metadata, stats, and suggestions.
export function generate(options: GenerateOptions): GenerateResult {
  const result = createGenerateResult();

  // Apply boundary value expansion to parameters that have boundary configs.
  const params = applyBoundaryExpansion(options);

  const hasInvalid = hasInvalidValues(params);

  const coverageResult = CoverageEngine.create(params, options.strength);
  if (coverageResult.error.code !== ErrorCode.Ok) {
    result.warnings.push(`${coverageResult.error.message}: ${coverageResult.error.detail}`);
    return result;
  }
  const coverage = coverageResult.engine;

  // Create sub-model engines.
  const subEngines: CoverageEngine[] = [];
  for (const sm of options.subModels) {
    const resolved = resolveParamNames(sm.parameterNames, params);
    if (resolved.error.length > 0) {
      result.warnings.push(resolved.error);
      return result;
    }
    if (resolved.indices.length < sm.strength) {
      result.warnings.push(
        'Sub-model strength (' +
          sm.strength +
          ') exceeds parameter count (' +
          resolved.indices.length +
          ')',
      );
      return result;
    }
    const smResult = CoverageEngine.createFromSubset(params, resolved.indices, sm.strength);
    if (smResult.error.code !== ErrorCode.Ok) {
      result.warnings.push(`${smResult.error.message}: ${smResult.error.detail}`);
      return result;
    }
    subEngines.push(smResult.engine);
  }

  // Parse constraint expressions into AST.
  const constraints: ConstraintNode[] = [];
  for (const expr of options.constraintExpressions) {
    const parseResult = parseConstraint(expr, params);
    if (parseResult.error.code !== ErrorCode.Ok) {
      result.warnings.push(`${parseResult.error.message}: ${parseResult.error.detail}`);
      return result;
    }
    constraints.push(parseResult.constraint!);
  }

  // Exclude tuples that are inherently invalid due to constraints.
  coverage.excludeInvalidTuples(constraints);
  for (const eng of subEngines) {
    eng.excludeInvalidTuples(constraints);
  }

  // Exclude tuples involving invalid values for positive generation.
  if (hasInvalid) {
    coverage.excludeInvalidValues();
    for (const eng of subEngines) {
      eng.excludeInvalidValues();
    }
  }

  // Build allowedValues mask for positive generation (valid values only).
  let validMask: boolean[][] = [];
  if (hasInvalid) {
    validMask = buildValidOnlyMask(params);
  }

  // Resolve value weights to index-based vectors.
  const resolvedWeights = resolveWeights(params, options.weights);

  const rng = new Rng(options.seed);

  // Pre-load seed tests into all engines.
  for (const seedTest of options.seeds) {
    coverage.addTestCase(seedTest);
    for (const eng of subEngines) {
      eng.addTestCase(seedTest);
    }
    result.tests.push(seedTest);
  }

  // Build scoring function that sums across all engines.
  let scoreFn: ScoreFn;
  if (subEngines.length === 0) {
    scoreFn = (partial, pi, vi) => {
      return coverage.scoreValue(partial, pi, vi);
    };
  } else {
    scoreFn = (partial, pi, vi) => {
      let score = coverage.scoreValue(partial, pi, vi);
      for (const eng of subEngines) {
        score += eng.scoreValue(partial, pi, vi);
      }
      return score;
    };
  }

  // Constructive greedy generation loop (positive tests only).
  const kMaxRetries = 50;
  let retries = 0;
  while (
    !allComplete(coverage, subEngines) &&
    (options.maxTests === 0 || result.tests.length < options.maxTests)
  ) {
    const tc = greedyConstruct(params, scoreFn, constraints, rng, validMask, resolvedWeights);
    const score = totalScore(coverage, subEngines, tc);
    if (score === 0) {
      if (++retries >= kMaxRetries) {
        break;
      }
      continue;
    }
    retries = 0;
    coverage.addTestCase(tc);
    for (const eng of subEngines) {
      eng.addTestCase(tc);
    }
    result.tests.push(tc);
  }

  // Generate negative tests if any parameter has invalid values.
  if (hasInvalid) {
    generateNegativeTests(params, options.strength, constraints, rng, result.negativeTests);
  }

  // Collect uncovered tuples from all engines.
  if (!allComplete(coverage, subEngines)) {
    const globalUncovered = coverage.getUncoveredTuples(params);
    for (const ut of globalUncovered) {
      result.uncovered.push(ut);
    }
    for (const eng of subEngines) {
      const subUncovered = eng.getUncoveredTuples(params);
      for (const ut of subUncovered) {
        result.uncovered.push(ut);
      }
    }
    for (const ut of result.uncovered) {
      const suggestion: Suggestion = {
        description: `Add test: ${uncoveredTupleToString(ut)}`,
        testCase: { values: [] },
      };
      result.suggestions.push(suggestion);
    }
  }

  // Report coverage as the minimum across all engines (for pass/fail).
  result.coverage = coverage.coverageRatio;
  for (const eng of subEngines) {
    result.coverage = Math.min(result.coverage, eng.coverageRatio);
  }
  result.stats.totalTuples = coverage.totalTuples;
  for (const eng of subEngines) {
    result.stats.totalTuples += eng.totalTuples;
  }
  result.stats.coveredTuples = coverage.coveredCount;
  for (const eng of subEngines) {
    result.stats.coveredTuples += eng.coveredCount;
  }
  result.stats.testCount = result.tests.length;

  return result;
}

/// Extend an existing test suite to improve coverage.
export function extend(
  existing: TestCase[],
  options: GenerateOptions,
  _mode: ExtendMode = ExtendMode.Strict,
): GenerateResult {
  const opts: GenerateOptions = {
    ...options,
    seeds: existing,
  };
  return generate(opts);
}

/// Estimate model statistics without running generation.
/// @param options The generation options to analyze.
/// @returns Model statistics including estimated test count.
export function estimateModel(options: GenerateOptions): ModelStats {
  // Apply boundary expansion for estimation.
  const params = applyBoundaryExpansion(options);

  const stats = createModelStats();
  stats.parameterCount = params.length;
  stats.strength = options.strength;
  stats.subModelCount = options.subModels.length;
  stats.constraintCount = options.constraintExpressions.length;

  let maxValues = 0;
  for (const p of params) {
    stats.totalValues += p.size;
    if (p.size > maxValues) {
      maxValues = p.size;
    }
    stats.parameters.push({
      name: p.name,
      valueCount: p.size,
      invalidCount: p.invalidCount,
    });
  }

  // Compute exact total tuples using CoverageEngine.
  const createResult = CoverageEngine.create(params, options.strength);
  if (createResult.error.code === ErrorCode.Ok) {
    stats.totalTuples = createResult.engine.totalTuples;
  }

  // Estimate test count.
  if (stats.parameterCount === 0) {
    stats.estimatedTests = 0;
  } else if (stats.parameterCount <= stats.strength) {
    let product = 1;
    for (const p of params) {
      product *= p.size;
    }
    stats.estimatedTests = product;
  } else {
    let estimate = 1;
    for (let i = 0; i < stats.strength; ++i) {
      estimate *= maxValues;
      if (estimate > 0xffffffff) {
        break;
      }
    }
    // Refine with log factor: roughly max_v^t * ceil(log2(n)).
    let logFactor = Math.ceil(Math.log2(stats.parameterCount));
    if (logFactor < 1) {
      logFactor = 1;
    }
    estimate *= logFactor;
    // Cap at totalTuples (can't need more tests than tuples).
    if (stats.totalTuples > 0 && estimate > stats.totalTuples) {
      estimate = stats.totalTuples;
    }
    stats.estimatedTests = Math.min(estimate, 0xffffffff) | 0;
  }

  return stats;
}
