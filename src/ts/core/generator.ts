/// @file generator.ts
/// @brief Main test generation orchestrator.

import { greedyConstruct, type ScoreFn } from '../algo/greedy.js';
import { expandBoundaryValues } from '../model/boundary.js';
import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { annotateConstraintError, parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode } from '../model/error.js';
import {
  createModelStats,
  ExtendMode,
  type GenerateOptions,
  isWeightConfigEmpty,
  type ModelStats,
  validateGenerateOptions,
  type WeightConfig,
} from '../model/generate-options.js';
import { hasInvalidValues, Parameter } from '../model/parameter.js';
import {
  createGenerateResult,
  type GenerateResult,
  type NegativeCoverage,
  type Suggestion,
  type TestCase,
  UNASSIGNED,
  uncoveredTupleToString,
} from '../model/test-case.js';
import { Rng } from '../util/rng.js';
import { annotateClassCoverage } from '../validator/coverage-validator.js';
import {
  completeAssignment,
  completeValidAssignment,
  createSolveBudget,
} from './constraint-solver.js';
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

/// Validate that a seed can participate in positive coverage.
function validatePositiveSeed(
  seed: TestCase,
  params: Parameter[],
  constraints: readonly ConstraintNode[],
): string {
  if (seed.values.length !== params.length) {
    return `expected ${params.length} value(s), got ${seed.values.length}`;
  }

  for (let pi = 0; pi < params.length; ++pi) {
    const vi = seed.values[pi];
    if (!Number.isInteger(vi) || vi < 0 || vi >= params[pi].size) {
      return `value index ${vi} is out of range for parameter ${params[pi].name}`;
    }
    if (params[pi].isInvalid(vi)) {
      return `value ${params[pi].name}=${params[pi].values[vi]} is marked invalid`;
    }
  }

  for (const constraint of constraints) {
    if (constraint.evaluate(seed.values) !== ConstraintResult.True) {
      return 'violates a constraint';
    }
  }

  return '';
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

/** Generate deterministic single-fault negative coverage using a reused engine. */
function generateNegativeTests(
  params: Parameter[],
  constraints: ConstraintNode[],
  freshCov: CoverageEngine,
  maxTests: number,
  positiveTestCount: number,
  negativeTests: TestCase[],
  metrics: NegativeCoverage,
  warnings: string[],
): { budgetExceeded: boolean } {
  let stoppedAtMaxTests = false;

  for (let pi = 0; pi < params.length; ++pi) {
    for (let vi = 0; vi < params[pi].size; ++vi) {
      if (!params[pi].isInvalid(vi)) {
        continue;
      }

      // Reset coverage/exclusion state for this invalid value, reusing the
      // precomputed tables.
      freshCov.resetCoverage();

      // Build mask: pi can only be vi, others can only be valid.
      const negMask = buildNegativeMask(params, pi, vi);
      freshCov.excludeTuplesOutsideMask(negMask);
      freshCov.excludeTuplesNotContaining(pi, vi);
      const exclusionBudget = { value: false };
      freshCov.excludeInvalidTuples(constraints, negMask, exclusionBudget);
      if (exclusionBudget.value) {
        return { budgetExceeded: true };
      }
      const noFeasibleTarget = freshCov.totalTuples === 0;

      // Use the same FirstUncovered + deterministic completion path as
      // positive generation, rather than a retry-limited random greedy pass.
      while (!freshCov.isComplete) {
        if (maxTests > 0 && positiveTestCount + negativeTests.length >= maxTests) {
          stoppedAtMaxTests = true;
          break;
        }
        const uncovered = freshCov.firstUncovered();
        if (uncovered === null) {
          break;
        }
        const budget = createSolveBudget();
        const witness = completeAssignment(
          params,
          constraints,
          negMask,
          { values: uncovered.assignment },
          budget,
        );
        if (witness === null) {
          if (budget.exceeded) {
            return { budgetExceeded: true };
          }
          freshCov.excludeTuple(uncovered.index);
          continue;
        }
        freshCov.addTestCase(witness);
        negativeTests.push(witness);
      }

      metrics.totalTuples += freshCov.totalTuples;
      metrics.coveredTuples += freshCov.coveredCount;
      if (!freshCov.isComplete || noFeasibleTarget) {
        warnings.push(
          `Negative coverage incomplete for ${params[pi].name}=${params[pi].values[vi]}`,
        );
      }
    }
  }
  metrics.omittedTuples = metrics.totalTuples - metrics.coveredTuples;
  metrics.coverageRatio =
    metrics.totalTuples === 0 ? 1 : metrics.coveredTuples / metrics.totalTuples;
  if (stoppedAtMaxTests) {
    warnings.push(
      `Negative generation stopped at maxTests (${maxTests}) before reaching full coverage`,
    );
  }
  return { budgetExceeded: false };
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
    const paramWeights = config.entries[params[pi].name];
    for (let vi = 0; vi < params[pi].size; ++vi) {
      // Resolve by key presence (not getWeight's 1.0 sentinel) so an explicit
      // weight of 1.0 is honored and a weight keyed by one of the value's aliases
      // is not silently dropped to the default.
      let w = 1.0;
      if (paramWeights !== undefined) {
        let entry = paramWeights[params[pi].values[vi]];
        if (entry === undefined) {
          for (const alias of params[pi].aliases(vi)) {
            if (paramWeights[alias] !== undefined) {
              entry = paramWeights[alias];
              break;
            }
          }
        }
        if (entry !== undefined) {
          w = entry;
        }
      }
      resolved[pi][vi] = w;
    }
  }
  return resolved;
}

/// Apply boundary value expansion to parameters with boundary configs.
///
/// When a parameter has a boundary config its value set is regenerated, so
/// aliases and equivalence classes (which are keyed to the original values) are
/// intentionally dropped — mirroring expandBoundaryValues. Otherwise the aliases
/// and classes carried on the options are restored on the rebuilt Parameter so
/// constraint resolution and class coverage see them.
function applyBoundaryExpansion(opts: GenerateOptions): { params: Parameter[]; seeds: TestCase[] } {
  const params: Parameter[] = [];
  for (const p of opts.parameters) {
    const param = p.invalid
      ? new Parameter(p.name, p.values, p.invalid)
      : new Parameter(p.name, p.values);
    const bc = opts.boundaryConfigs[p.name];
    if (bc) {
      params.push(expandBoundaryValues(param, bc));
    } else {
      if (p.aliases?.some((a) => a.length > 0)) {
        param.setAliases(p.aliases);
      }
      if (p.equivalenceClasses?.some((c) => c.length > 0)) {
        param.setEquivalenceClasses(p.equivalenceClasses);
      }
      params.push(param);
    }
  }
  const seeds = opts.seeds.map((seed) => {
    const values = [...seed.values];
    for (let pi = 0; pi < values.length && pi < opts.parameters.length; ++pi) {
      const oldIndex = values[pi];
      const oldParam = opts.parameters[pi];
      if (!Number.isInteger(oldIndex) || oldIndex < 0 || oldIndex >= oldParam.values.length) {
        continue;
      }
      const oldValue = oldParam.values[oldIndex];
      let newIndex = params[pi].findValueIndex(oldValue);
      if (newIndex === UNASSIGNED && Number.isFinite(Number(oldValue))) {
        newIndex = params[pi].values.findIndex(
          (candidate) =>
            Number.isFinite(Number(candidate)) && Number(candidate) === Number(oldValue),
        );
      }
      if (newIndex !== UNASSIGNED && newIndex >= 0) {
        values[pi] = newIndex;
      }
    }
    return { values };
  });
  return { params, seeds };
}

/// Generate a covering array for the given options.
/// @returns The generated test suite with coverage metadata, stats, and suggestions.
function generateImpl(options: GenerateOptions, preservedSeedCount: number): GenerateResult {
  const result = createGenerateResult();

  result.error = validateGenerateOptions(options);
  if (result.error.code !== ErrorCode.Ok) {
    result.warnings.push(
      result.error.detail
        ? `${result.error.message}: ${result.error.detail}`
        : result.error.message,
    );
    return result;
  }

  // Apply boundary value expansion to parameters that have boundary configs.
  const expanded = applyBoundaryExpansion(options);
  const params = expanded.params;
  result.parameters = params;

  const hasInvalid = hasInvalidValues(params);

  const coverageResult = CoverageEngine.create(params, options.strength);
  if (coverageResult.error.code !== ErrorCode.Ok) {
    result.warnings.push(`${coverageResult.error.message}: ${coverageResult.error.detail}`);
    result.error = coverageResult.error;
    return result;
  }
  const coverage = coverageResult.engine;
  let allocatedTuples = coverage.totalTuples;

  // Create sub-model engines.
  const subEngines: CoverageEngine[] = [];
  for (const sm of options.subModels) {
    const resolved = resolveParamNames(sm.parameterNames, params);
    if (resolved.error.length > 0) {
      result.warnings.push(resolved.error);
      result.error = { code: ErrorCode.InvalidInput, message: resolved.error, detail: '' };
      return result;
    }
    if (resolved.indices.length < sm.strength) {
      const msg =
        'Sub-model strength (' +
        sm.strength +
        ') exceeds parameter count (' +
        resolved.indices.length +
        ')';
      result.warnings.push(msg);
      result.error = { code: ErrorCode.InvalidInput, message: msg, detail: '' };
      return result;
    }
    const smResult = CoverageEngine.createFromSubset(params, resolved.indices, sm.strength);
    if (smResult.error.code !== ErrorCode.Ok) {
      result.warnings.push(`${smResult.error.message}: ${smResult.error.detail}`);
      result.error = smResult.error;
      return result;
    }
    if (smResult.engine.totalTuples > CoverageEngine.MAX_TUPLES - allocatedTuples) {
      result.error = {
        code: ErrorCode.TupleExplosion,
        message: 'Combined global and sub-model tuple count exceeds safe limit',
        detail: `limit=${CoverageEngine.MAX_TUPLES}`,
      };
      result.warnings.push(`${result.error.message}: ${result.error.detail}`);
      return result;
    }
    allocatedTuples += smResult.engine.totalTuples;
    subEngines.push(smResult.engine);
  }

  // Parse constraint expressions into AST.
  const constraints: ConstraintNode[] = [];
  for (const expr of options.constraintExpressions) {
    const parseResult = parseConstraint(expr, params);
    if (parseResult.error.code !== ErrorCode.Ok) {
      const err = annotateConstraintError(expr, parseResult.error);
      result.warnings.push(`${err.message}: ${err.detail}`);
      result.error = err;
      return result;
    }
    if (parseResult.constraint == null) {
      result.warnings.push('Constraint parser returned no constraint');
      result.error = {
        code: ErrorCode.ConstraintError,
        message: 'Constraint parser returned no constraint',
        detail: '',
      };
      return result;
    }
    constraints.push(parseResult.constraint);
  }

  if (constraints.length > 0) {
    const budget = createSolveBudget();
    const witness = completeValidAssignment(
      params,
      constraints,
      { values: new Array(params.length).fill(UNASSIGNED) },
      budget,
    );
    if (witness === null) {
      result.error = budget.exceeded
        ? {
            code: ErrorCode.ConstraintError,
            message: 'Constraint search budget exceeded',
            detail: 'The constraint model is too complex to solve within the search budget',
          }
        : {
            code: ErrorCode.ConstraintError,
            message: 'Constraints are unsatisfiable',
            detail: 'No complete assignment using valid values satisfies all constraints',
          };
      result.warnings.push(`${result.error.message}: ${result.error.detail}`);
      return result;
    }
  }

  // Exclude tuples that are inherently invalid due to constraints.
  const excludeBudgetExceeded = { value: false };
  coverage.excludeInvalidTuples(constraints, [], excludeBudgetExceeded);
  for (const eng of subEngines) {
    eng.excludeInvalidTuples(constraints, [], excludeBudgetExceeded);
  }
  if (excludeBudgetExceeded.value) {
    result.error = {
      code: ErrorCode.ConstraintError,
      message: 'Constraint search budget exceeded',
      detail: 'Tuple feasibility could not be determined within the search budget',
    };
    result.warnings.push(`${result.error.message}: ${result.error.detail}`);
    return result;
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

  // Pre-load seed tests into all engines. Strict extension retains the existing
  // prefix even when a row no longer matches the model, but such rows do not
  // contribute to coverage.
  let droppedForMaxTests = false;
  for (let si = 0; si < expanded.seeds.length; ++si) {
    const seedTest = expanded.seeds[si];
    if (options.maxTests > 0 && result.tests.length >= options.maxTests) {
      droppedForMaxTests = true;
      break;
    }
    const seedError = validatePositiveSeed(seedTest, params, constraints);
    if (seedError.length > 0) {
      if (si < preservedSeedCount) {
        result.tests.push(seedTest);
        result.warnings.push(
          `Existing test ${si} preserved but excluded from coverage: ${seedError}`,
        );
      } else {
        result.warnings.push(`Seed test ${si - preservedSeedCount} ignored: ${seedError}`);
      }
      continue;
    }
    coverage.addTestCase(seedTest);
    for (const eng of subEngines) {
      eng.addTestCase(seedTest);
    }
    result.tests.push(seedTest);
  }

  if (droppedForMaxTests) {
    result.warnings.push(
      `Seed test count (${expanded.seeds.length}) exceeds maxTests (${options.maxTests}); some seeds were dropped`,
    );
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
    // A failed construction (no constraint-satisfying value for some parameter)
    // is treated like a zero-score candidate: retry with a different shuffle.
    if (tc === null) {
      if (++retries >= kMaxRetries) {
        break;
      }
      continue;
    }
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

  // Deterministic completion phase. Randomized greedy construction can stall on
  // hard-to-reach tuples (notably t === parameter count, or tightly constrained
  // models), abandoning tuples that are in fact coverable and leaving coverage
  // below 100%. For each remaining uncovered tuple, build a test that covers it
  // directly by fixing the tuple's values and completing the rest with a
  // constraint feasibility search. A tuple that cannot be completed is genuinely
  // infeasible and is excluded from the coverage target so it no longer counts
  // as a shortfall.
  let completionBudgetExceeded = false;
  const completePartial = (partial: number[]): TestCase | null => {
    const budget = createSolveBudget();
    const witness = hasInvalid
      ? completeAssignment(params, constraints, validMask, { values: partial }, budget)
      : completeValidAssignment(params, constraints, { values: partial }, budget);
    if (budget.exceeded) {
      completionBudgetExceeded = true;
    }
    return witness;
  };
  const pickIncomplete = (): CoverageEngine | null => {
    if (!coverage.isComplete) {
      return coverage;
    }
    for (const eng of subEngines) {
      if (!eng.isComplete) {
        return eng;
      }
    }
    return null;
  };
  for (let eng = pickIncomplete(); eng !== null; eng = pickIncomplete()) {
    if (options.maxTests > 0 && result.tests.length >= options.maxTests) {
      break;
    }
    const ua = eng.firstUncovered();
    if (ua === null) {
      break; // Defensive: isComplete disagreed.
    }
    const witness = completePartial(ua.assignment);
    if (completionBudgetExceeded) {
      break;
    }
    if (witness === null) {
      // Partial-feasible but not extensible to a full satisfying assignment.
      eng.excludeTuple(ua.index);
      continue;
    }
    coverage.addTestCase(witness);
    for (const e of subEngines) {
      e.addTestCase(witness);
    }
    result.tests.push(witness);
  }
  if (completionBudgetExceeded) {
    result.error = {
      code: ErrorCode.ConstraintError,
      message: 'Constraint search budget exceeded',
      detail: 'A coverage witness could not be found within the search budget',
    };
    result.warnings.push(`${result.error.message}: ${result.error.detail}`);
    return result;
  }

  // Warn if generation stopped before reaching full coverage. After the
  // completion phase this can only happen when maxTests bounds the suite.
  if (!allComplete(coverage, subEngines)) {
    if (options.maxTests > 0 && result.tests.length >= options.maxTests) {
      result.warnings.push(
        `Generation stopped at maxTests (${options.maxTests}) before reaching 100% coverage`,
      );
    } else {
      result.warnings.push('Generation stopped before reaching 100% coverage');
    }
  }

  // Collect uncovered tuples from all engines.
  if (!allComplete(coverage, subEngines)) {
    result.uncoveredCount = coverage.totalTuples - coverage.coveredCount;
    for (const eng of subEngines) {
      result.uncoveredCount += eng.totalTuples - eng.coveredCount;
    }
    const globalUncovered = coverage.getUncoveredTuples(params);
    for (const ut of globalUncovered) {
      result.uncovered.push(ut);
    }
    for (const eng of subEngines) {
      const remaining = Math.max(0, CoverageEngine.MAX_DIAGNOSTIC_TUPLES - result.uncovered.length);
      const subUncovered = eng.getUncoveredTuples(params, remaining);
      for (const ut of subUncovered) {
        result.uncovered.push(ut);
      }
    }
    result.omittedUncovered = result.uncoveredCount - result.uncovered.length;
    for (const ut of result.uncovered) {
      if (result.suggestions.length >= 100) {
        break;
      }
      const partial = new Array<number>(params.length).fill(UNASSIGNED);
      // Reconstruct the witness from (param, value) indices rather than parsing
      // "name=value" strings, which is ambiguous when a name or value holds '='.
      for (const [pi, vi] of ut.indices ?? []) {
        if (pi < partial.length) {
          partial[pi] = vi;
        }
      }
      const witness = completeValidAssignment(params, constraints, { values: partial });
      if (witness === null) {
        continue;
      }
      const suggestion: Suggestion = {
        description: `Add test: ${uncoveredTupleToString(ut)}`,
        testCase: witness,
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

  // Use the exact parsed constraints and effective parameters so all wrappers
  // expose identical class-coverage semantics for generate and extend.
  annotateClassCoverage(result, params, options.strength, constraints);
  if (result.error.code !== ErrorCode.Ok) {
    return result;
  }

  // Positive coverage no longer needs the global engine after this point, so
  // reuse its bitmap for negative coverage instead of allocating a second one.
  if (hasInvalid) {
    const negativeCoverage: NegativeCoverage = {
      totalTuples: 0,
      coveredTuples: 0,
      omittedTuples: 0,
      coverageRatio: 1,
    };
    const negativeOutcome = generateNegativeTests(
      params,
      constraints,
      coverage,
      options.maxTests,
      result.tests.length,
      result.negativeTests,
      negativeCoverage,
      result.warnings,
    );
    result.negativeCoverage = negativeCoverage;
    result.stats.testCount = result.tests.length + result.negativeTests.length;
    if (negativeOutcome.budgetExceeded) {
      result.error = {
        code: ErrorCode.ConstraintError,
        message: 'Constraint search budget exceeded',
        detail: 'A negative coverage witness could not be found within the search budget',
      };
      result.warnings.push(`${result.error.message}: ${result.error.detail}`);
    }
  }

  return result;
}

export function generate(options: GenerateOptions): GenerateResult {
  return generateImpl(options, 0);
}

/// Extend an existing test suite to improve coverage.
export function extend(
  existing: TestCase[],
  options: GenerateOptions,
  mode: ExtendMode = ExtendMode.Strict,
): GenerateResult {
  if (mode !== ExtendMode.Strict) {
    const result = createGenerateResult();
    result.error = {
      code: ErrorCode.InvalidInput,
      message: `Unsupported extend mode: ${String(mode)}`,
      detail: 'Supported modes: strict',
    };
    result.warnings.push(`${result.error.message}: ${result.error.detail}`);
    return result;
  }
  if (options.maxTests > 0 && existing.length > options.maxTests) {
    const result = createGenerateResult();
    result.error = {
      code: ErrorCode.InvalidInput,
      message: 'maxTests cannot be smaller than the existing test count',
      detail: `maxTests=${options.maxTests}, existing=${existing.length}`,
    };
    result.warnings.push(`${result.error.message}: ${result.error.detail}`);
    return result;
  }
  const opts: GenerateOptions = {
    ...options,
    seeds: [...existing, ...options.seeds],
  };
  return generateImpl(opts, existing.length);
}

/// Estimate model statistics without running generation.
/// @param options The generation options to analyze.
/// @returns Model statistics including estimated test count.
export function estimateModel(options: GenerateOptions): ModelStats {
  const stats = createModelStats();
  stats.error = validateGenerateOptions(options);
  if (stats.error.code !== ErrorCode.Ok) {
    return stats;
  }

  // Apply boundary expansion for estimation.
  const params = applyBoundaryExpansion(options).params;

  // Keep tuple counts raw, while matching generate's constraint syntax and
  // reference validation contract.
  for (const expression of options.constraintExpressions) {
    const parsed = parseConstraint(expression, params);
    if (parsed.error.code !== ErrorCode.Ok) {
      stats.error = annotateConstraintError(expression, parsed.error);
      return stats;
    }
  }

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

  // Raw global + sub-model upper bound before constraint exclusion, using the
  // same combined allocation budget as generation.
  const createResult = CoverageEngine.create(params, options.strength);
  if (createResult.error.code !== ErrorCode.Ok) {
    stats.error = createResult.error;
    return stats;
  }
  stats.totalTuples = createResult.engine.totalTuples;
  for (const subModel of options.subModels) {
    const resolved = resolveParamNames(subModel.parameterNames, params);
    if (resolved.error.length > 0) {
      stats.error = { code: ErrorCode.InvalidInput, message: resolved.error, detail: '' };
      return stats;
    }
    const subResult = CoverageEngine.createFromSubset(params, resolved.indices, subModel.strength);
    if (subResult.error.code !== ErrorCode.Ok) {
      stats.error = subResult.error;
      return stats;
    }
    if (subResult.engine.totalTuples > CoverageEngine.MAX_TUPLES - stats.totalTuples) {
      stats.error = {
        code: ErrorCode.TupleExplosion,
        message: 'Combined global and sub-model tuple count exceeds safe limit',
        detail: `limit=${CoverageEngine.MAX_TUPLES}`,
      };
      return stats;
    }
    stats.totalTuples += subResult.engine.totalTuples;
  }

  // Estimate test count.
  if (stats.parameterCount === 0) {
    stats.estimatedTests = 0;
  } else if (stats.parameterCount <= stats.strength) {
    let product = 1;
    for (const p of params) {
      product *= p.size;
      if (product > 0xffffffff) {
        break;
      }
    }
    stats.estimatedTests = Math.min(product, 0xffffffff) >>> 0;
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
    stats.estimatedTests = Math.min(estimate, 0xffffffff) >>> 0;
  }

  return stats;
}
