/// @file generator.ts
/// @brief Main test generation orchestrator.

import {
  createGreedyScratch,
  type GreedyScratch,
  greedyConstruct,
  type ScoreValuesFn,
} from '../algo/greedy.js';
import type { ConstraintNode } from '../model/constraint-ast.js';
import { annotateConstraintError, parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode, type ErrorInfo, okError, surfaceErrorText } from '../model/error.js';
import {
  type ChargedText,
  createModelStats,
  ExtendMode,
  type GenerateOptions,
  isWeightConfigEmpty,
  type ModelStats,
  NO_CHARGED_TEXT,
  optionsParameters,
  type WeightConfig,
} from '../model/generate-options.js';
import { hasInvalidValues, type Parameter, ValueResolver } from '../model/parameter.js';
import {
  createGenerateResult,
  type GenerateResult,
  type NegativeCoverage,
  type Suggestion,
  type TestCase,
  UNASSIGNED,
  type UncoveredTuple,
  uncoveredTupleToString,
} from '../model/test-case.js';
import { MAX_DIAGNOSTIC_TUPLES, MAX_TUPLES } from '../model/tuning-limits.js';
import { Rng } from '../util/rng.js';
import { annotateClassCoverage } from '../validator/coverage-validator.js';
import {
  buildAllowedSolveParameterOrder,
  buildValidSolveParameterOrder,
  completeAssignment,
  completeValidAssignment,
  createSolveBudget,
  createSolveStack,
  type SolveParameterOrder,
  type SolveStack,
} from './constraint-solver.js';
import { CoverageEngine, countTuples } from './coverage-engine.js';
import { classifySeedRow, EngineInput } from './engine-input.js';

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

/// The tuple space one coverage engine enumerates.
///
/// A tuple is identified by its parameter set and value tuple, not by the engine
/// that reports it, so two engines describe the same interaction exactly when
/// their shapes overlap. Kept beside the engines because the engine itself does
/// not expose its subset or strength.
interface EngineShape {
  /// Global parameter indices, ascending.
  params: number[];
  strength: number;
}

/// Whether two engines can enumerate a common tuple.
///
/// Tuple identity includes the parameter set, so engines of different strengths
/// never collide, and engines sharing fewer than `strength` parameters have no
/// parameter combination in common either.
function shapesOverlap(a: EngineShape, b: EngineShape): boolean {
  if (a.strength !== b.strength) {
    return false;
  }
  let shared = 0;
  let i = 0;
  let j = 0;
  while (i < a.params.length && j < b.params.length) {
    if (a.params[i] === b.params[j]) {
      ++shared;
      if (shared >= a.strength) {
        return true;
      }
      ++i;
      ++j;
    } else if (a.params[i] < b.params[j]) {
      ++i;
    } else {
      ++j;
    }
  }
  return a.strength === 0;
}

/// Identity key of a tuple: (parameter index, value index) pairs in ascending
/// parameter order, rendered so distinct tuples compare as distinct strings.
function tupleKey(indices: Array<[number, number]> | undefined): string {
  return (indices ?? []).map(([pi, vi]) => `${pi}:${vi}`).join(',');
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

/// Scratch state held for the length of one generation pass.
///
/// Everything a hot loop would otherwise rebuild per iteration is owned here and
/// borrowed by the loop: the greedy construction buffers and the feasibility
/// solver's parameter order and frame stack. The order depends only on the
/// allowed-value mask, so it changes once per phase (or invalid value) and never
/// per witness.
interface GenerationScratch {
  greedy: GreedyScratch;
  solveOrder: SolveParameterOrder;
  solveStack: SolveStack;
}

/// Consecutive constructions that may gain nothing before greedy gives way to
/// deterministic completion.
const MAX_GREEDY_RETRIES = 50;

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

/**
 * Generate single-fault negative coverage using a reused engine.
 *
 * Rows are chosen by the same coverage-gain construction as positive rows,
 * with deterministic completion covering whatever greedy leaves.
 *
 * @param preservedNegatives Single-fault rows already in the suite; their
 *   tuples are credited rather than generated again.
 * @returns An ok error on success, or the constraint-budget error that stopped
 *   the pass. The two budget exits are reported apart: failing to classify the
 *   target universe and failing to find a witness for a classified target are
 *   distinguishable outcomes, and the C++ core reports them as such.
 */
function generateNegativeTests(
  params: Parameter[],
  constraints: ConstraintNode[],
  weights: number[][],
  preservedNegatives: readonly TestCase[],
  freshCov: CoverageEngine,
  maxTests: number,
  positiveTestCount: number,
  rng: Rng,
  scratch: GenerationScratch,
  negativeTests: TestCase[],
  metrics: NegativeCoverage,
  warnings: string[],
): ErrorInfo {
  let stoppedAtMaxTests = false;
  const atMaxTests = (): boolean =>
    maxTests > 0 && positiveTestCount + negativeTests.length >= maxTests;
  const scoreValues: ScoreValuesFn = (partial, pi, scores) => {
    freshCov.addValueScores(partial, pi, scores);
  };

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
        return {
          code: ErrorCode.ConstraintError,
          message: 'Constraint search budget exceeded',
          detail: 'Negative coverage targets could not be classified within the search budget',
        };
      }
      const noFeasibleTarget = freshCov.totalTuples === 0;

      // A single-fault row's only invalid value is the one it holds here.
      for (const row of preservedNegatives) {
        if (row.values[pi] === vi) {
          freshCov.addTestCase(row);
        }
      }

      // The mask is fixed for the whole inner loop, so the solver order derived
      // from it is built once here rather than once per witness.
      scratch.solveOrder = buildAllowedSolveParameterOrder(params, negMask);

      let retries = 0;
      while (!freshCov.isComplete) {
        if (atMaxTests()) {
          stoppedAtMaxTests = true;
          break;
        }
        const built = greedyConstruct(
          params,
          scoreValues,
          constraints,
          rng,
          scratch.greedy,
          negMask,
          weights,
        );
        if (built === null || built.score === 0) {
          if (++retries >= MAX_GREEDY_RETRIES) {
            break;
          }
          continue;
        }
        retries = 0;
        freshCov.addTestCase(built.testCase);
        negativeTests.push(built.testCase);
      }

      // Cover every feasible tuple greedy left, by the same deterministic
      // firstUncovered + completeAssignment path positive generation uses.
      while (!freshCov.isComplete) {
        if (atMaxTests()) {
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
          scratch.solveOrder,
          scratch.solveStack,
        );
        if (witness === null) {
          if (budget.exceeded) {
            return {
              code: ErrorCode.ConstraintError,
              message: 'Constraint search budget exceeded',
              detail: 'A negative coverage witness could not be found within the search budget',
            };
          }
          freshCov.excludeTuple(uncovered.index);
          continue;
        }
        freshCov.addTestCase(witness);
        negativeTests.push(witness);
      }

      metrics.totalTuples += freshCov.totalTuples;
      metrics.coveredTuples += freshCov.coveredCount;
      // Two different facts, reported as two different warnings. A value with no
      // feasible target contributes nothing to the metrics, so calling that
      // "incomplete" contradicts the very numbers the caller is told to read it
      // against: the metrics would show the whole universe covered while the
      // warning claimed a shortfall.
      const valueName = `${params[pi].name}=${params[pi].values[vi]}`;
      if (!freshCov.isComplete) {
        warnings.push(`Negative coverage incomplete for ${valueName}`);
      } else if (noFeasibleTarget) {
        warnings.push(`No feasible negative coverage target for ${valueName}`);
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
  return okError();
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
    const paramWeights = Object.hasOwn(config.entries, params[pi].name)
      ? config.entries[params[pi].name]
      : undefined;
    resolved[pi].fill(1.0);
    if (paramWeights === undefined) {
      continue;
    }
    // Resolve the caller's keys rather than probing the map with each declared
    // spelling: a weights key is a value name the caller wrote, so it names its
    // value through resolveValueName -- by any ASCII case, and by an alias --
    // which is the same reading the acceptance gate gave it. Object.entries
    // yields own enumerable keys only, so a parameter or value named after an
    // Object.prototype member is not read as a configured weight, and a key
    // present with an explicit weight of 1.0 is honored rather than mistaken
    // for the default.
    const named = new Array<boolean>(params[pi].size).fill(false);
    const resolver = new ValueResolver(params[pi]);
    for (const [valueName, weight] of Object.entries(paramWeights)) {
      const vi = resolver.resolve(valueName);
      if (vi === UNASSIGNED) {
        continue;
      }
      // A value named by more than one key keeps the weight written under the
      // spelling the model declares, as it did when that spelling was probed
      // ahead of the aliases.
      if (named[vi] && params[pi].values[vi] !== valueName) {
        continue;
      }
      resolved[pi][vi] = weight;
      named[vi] = true;
    }
  }
  return resolved;
}

/// The result of a call whose options never got past the gate.
function rejectedResult(options: GenerateOptions, error: ErrorInfo): GenerateResult {
  const result = createGenerateResult();
  result.parameters = optionsParameters(options);
  result.error = error;
  result.warnings.push(surfaceErrorText(error));
  return result;
}

/// Generate a covering array for the given options.
/// @returns The generated test suite with coverage metadata, stats, and suggestions.
function generateImpl(input: EngineInput): GenerateResult {
  const options = input.options;
  const params = input.params;

  const result = createGenerateResult();
  result.parameters = params;

  const hasInvalid = hasInvalidValues(params);

  const coverageResult = CoverageEngine.create(params, options.strength);
  if (coverageResult.error.code !== ErrorCode.Ok) {
    result.warnings.push(surfaceErrorText(coverageResult.error));
    result.error = coverageResult.error;
    return result;
  }
  const coverage = coverageResult.engine;
  let allocatedTuples = coverage.totalTuples;

  // Create sub-model engines. Their tuple spaces are recorded alongside so the
  // uncovered diagnostics can tell overlapping engines apart from disjoint ones.
  const subEngines: CoverageEngine[] = [];
  const subShapes: EngineShape[] = [];
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
      result.warnings.push(surfaceErrorText(smResult.error));
      result.error = smResult.error;
      return result;
    }
    if (smResult.engine.totalTuples > MAX_TUPLES - allocatedTuples) {
      result.error = {
        code: ErrorCode.TupleExplosion,
        message: 'Combined global and sub-model tuple count exceeds safe limit',
        detail: `limit=${MAX_TUPLES}`,
      };
      result.warnings.push(surfaceErrorText(result.error));
      return result;
    }
    allocatedTuples += smResult.engine.totalTuples;
    subEngines.push(smResult.engine);
    subShapes.push({ params: resolved.indices, strength: sm.strength });
  }

  // Parse constraint expressions into AST.
  const constraints: ConstraintNode[] = [];
  for (const expr of options.constraintExpressions) {
    const parseResult = parseConstraint(expr, params);
    if (parseResult.error.code !== ErrorCode.Ok) {
      const err = annotateConstraintError(expr, parseResult.error);
      result.warnings.push(surfaceErrorText(err));
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

  // This valid-value order serves the initial satisfiability check, positive
  // completion, and suggestions. The frame stack is reused for every search.
  const solveOrder = buildValidSolveParameterOrder(params);
  const solveStack = createSolveStack();

  if (constraints.length > 0) {
    const budget = createSolveBudget();
    const witness = completeValidAssignment(
      params,
      constraints,
      { values: new Array(params.length).fill(UNASSIGNED) },
      budget,
      solveOrder,
      solveStack,
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
      result.warnings.push(surfaceErrorText(result.error));
      return result;
    }
  }

  // Exclude tuples that are inherently invalid due to constraints.
  const excludeBudgetExceeded = { value: false };
  coverage.excludeInvalidTuples(constraints, [], excludeBudgetExceeded);
  for (const eng of subEngines) {
    if (excludeBudgetExceeded.value) {
      break;
    }
    eng.excludeInvalidTuples(constraints, [], excludeBudgetExceeded);
  }
  if (excludeBudgetExceeded.value) {
    result.error = {
      code: ErrorCode.ConstraintError,
      message: 'Constraint search budget exceeded',
      detail: 'Tuple feasibility could not be determined within the search budget',
    };
    result.warnings.push(surfaceErrorText(result.error));
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
  // prefix even when a row no longer matches the model; such rows do not
  // contribute to positive coverage, and a preserved single-fault row is
  // credited to the negative pass instead.
  let droppedForMaxTests = false;
  const preservedNegatives: TestCase[] = [];
  const seedRows = input.seedRows;
  for (const row of seedRows) {
    if (options.maxTests > 0 && result.tests.length >= options.maxTests) {
      droppedForMaxTests = true;
      break;
    }
    const classification = classifySeedRow(row, params, constraints);
    if (classification.error.length > 0) {
      if (row.preserved) {
        result.tests.push(row.test);
        result.warnings.push(
          `Existing test ${row.sourceIndex} preserved but excluded from coverage: ${classification.error}`,
        );
        if (classification.singleFault) {
          preservedNegatives.push(row.test);
        }
      } else {
        result.warnings.push(`Seed test ${row.sourceIndex} ignored: ${classification.error}`);
      }
      continue;
    }
    coverage.addTestCase(row.test);
    for (const eng of subEngines) {
      eng.addTestCase(row.test);
    }
    result.tests.push(row.test);
  }

  if (droppedForMaxTests) {
    result.warnings.push(
      `Seed test count (${seedRows.length}) exceeds maxTests (${options.maxTests}); some seeds were dropped`,
    );
  }

  // Scratch borrowed by the construction and completion loops below.
  const scratch: GenerationScratch = {
    greedy: createGreedyScratch(params),
    solveOrder,
    solveStack,
  };

  // Build scoring function that sums across all engines.
  let scoreValues: ScoreValuesFn;
  if (subEngines.length === 0) {
    scoreValues = (partial, pi, scores) => {
      coverage.addValueScores(partial, pi, scores);
    };
  } else {
    scoreValues = (partial, pi, scores) => {
      coverage.addValueScores(partial, pi, scores);
      for (const eng of subEngines) {
        eng.addValueScores(partial, pi, scores);
      }
    };
  }

  // Constructive greedy generation loop (positive tests only).
  let retries = 0;
  while (
    !allComplete(coverage, subEngines) &&
    (options.maxTests === 0 || result.tests.length < options.maxTests)
  ) {
    const built = greedyConstruct(
      params,
      scoreValues,
      constraints,
      rng,
      scratch.greedy,
      validMask,
      resolvedWeights,
    );
    // A failed construction (no constraint-satisfying value for some parameter)
    // is treated like a zero-score candidate: retry with a different shuffle.
    if (built === null) {
      if (++retries >= MAX_GREEDY_RETRIES) {
        break;
      }
      continue;
    }
    // Construction already summed the gain of every combination it completed,
    // and coverage does not change while a test case is being built, so the
    // candidate needs no second full scan of the coverage bitmaps.
    if (built.score === 0) {
      if (++retries >= MAX_GREEDY_RETRIES) {
        break;
      }
      continue;
    }
    const tc = built.testCase;
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
      ? completeAssignment(
          params,
          constraints,
          validMask,
          { values: partial },
          budget,
          scratch.solveOrder,
          scratch.solveStack,
        )
      : completeValidAssignment(
          params,
          constraints,
          { values: partial },
          budget,
          scratch.solveOrder,
          scratch.solveStack,
        );
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
    result.warnings.push(surfaceErrorText(result.error));
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

  // Collect uncovered tuples from all engines. A sub-model can enumerate the
  // same interaction as the global model or as another sub-model, so both the
  // total and the diagnostic list describe the union over engines: every
  // distinct tuple is counted once, listed once, and suggested once.
  if (!allComplete(coverage, subEngines)) {
    const globalShape: EngineShape = {
      params: params.map((_, pi) => pi),
      strength: options.strength,
    };

    // The global engine is counted first, so every tuple it needs is new. An
    // engine whose tuple space is disjoint from all earlier ones contributes its
    // whole shortfall; only an overlap has to be resolved tuple by tuple.
    result.uncoveredCount = coverage.totalTuples - coverage.coveredCount;
    for (let i = 0; i < subEngines.length; ++i) {
      const earlier: CoverageEngine[] = [];
      if (shapesOverlap(subShapes[i], globalShape)) {
        earlier.push(coverage);
      }
      for (let j = 0; j < i; ++j) {
        if (shapesOverlap(subShapes[i], subShapes[j])) {
          earlier.push(subEngines[j]);
        }
      }
      const shortfall = subEngines[i].totalTuples - subEngines[i].coveredCount;
      if (earlier.length === 0) {
        result.uncoveredCount += shortfall;
        continue;
      }
      // The overlap is resolved on plain (parameter, value) indices. Building
      // the human-readable form here would tie the cost of a count to the
      // shortfall itself; that form belongs to the diagnostic list below, which
      // is bounded by MAX_DIAGNOSTIC_TUPLES.
      subEngines[i].forEachUncoveredTuple((combo, valueIndices) => {
        if (!earlier.some((eng) => eng.needsTuple(combo, valueIndices))) {
          ++result.uncoveredCount;
        }
        return true;
      });
    }

    // Fill the diagnostic budget with distinct tuples: each engine is asked for
    // a full budget's worth rather than the remaining slots, so tuples already
    // listed by an earlier engine do not shrink the report.
    const listed = new Set<string>();
    const appendDistinct = (tuples: UncoveredTuple[]): void => {
      for (const ut of tuples) {
        if (result.uncovered.length >= MAX_DIAGNOSTIC_TUPLES) {
          return;
        }
        const key = tupleKey(ut.indices);
        if (listed.has(key)) {
          continue;
        }
        listed.add(key);
        result.uncovered.push(ut);
      }
    };
    appendDistinct(coverage.getUncoveredTuples(params));
    for (const eng of subEngines) {
      if (result.uncovered.length >= MAX_DIAGNOSTIC_TUPLES) {
        break;
      }
      appendDistinct(eng.getUncoveredTuples(params, MAX_DIAGNOSTIC_TUPLES));
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
      const witness = completeValidAssignment(
        params,
        constraints,
        { values: partial },
        undefined,
        scratch.solveOrder,
        scratch.solveStack,
      );
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
    const negativeError = generateNegativeTests(
      params,
      constraints,
      resolvedWeights,
      preservedNegatives,
      coverage,
      options.maxTests,
      result.tests.length,
      rng,
      scratch,
      result.negativeTests,
      negativeCoverage,
      result.warnings,
    );
    // A pass that stopped on an exhausted search budget never reached the
    // omitted/ratio finalization, so its counters do not describe a whole tuple
    // universe. Leave the field unset instead of publishing a self-contradictory
    // report, matching how class coverage reports its own failures.
    if (negativeError.code === ErrorCode.Ok) {
      result.negativeCoverage = negativeCoverage;
    }
    result.stats.testCount = result.tests.length + result.negativeTests.length;
    if (negativeError.code !== ErrorCode.Ok) {
      result.error = negativeError;
      result.warnings.push(surfaceErrorText(result.error));
    }
  }

  return result;
}

export function generate(options: GenerateOptions): GenerateResult {
  const accepted = EngineInput.accept(options, [], NO_CHARGED_TEXT);
  if (accepted.input === null) {
    return rejectedResult(options, accepted.error);
  }
  return generateImpl(accepted.input);
}

/// Extend an existing test suite to improve coverage.
///
/// @param charged What the caller already counted against the string budget.
///   A surface that read `existing` and `seeds` itself passes its row total, so
///   their text is charged once; the default has the gate charge the rows'
///   unresolved text. Mirrors core::Extend in the C++ core.
export function extend(
  existing: TestCase[],
  options: GenerateOptions,
  mode: ExtendMode = ExtendMode.Strict,
  charged: ChargedText = NO_CHARGED_TEXT,
): GenerateResult {
  if (mode !== ExtendMode.Strict) {
    return rejectedResult(options, {
      code: ErrorCode.InvalidInput,
      message: `Unsupported extend mode: ${String(mode)}`,
      detail: 'Supported modes: strict',
    });
  }
  const accepted = EngineInput.accept(options, existing, charged);
  if (accepted.input === null) {
    return rejectedResult(options, accepted.error);
  }
  return generateImpl(accepted.input);
}

/// Estimate model statistics without running generation.
/// @param options The generation options to analyze.
/// @returns Model statistics including estimated test count.
export function estimateModel(options: GenerateOptions): ModelStats {
  const accepted = EngineInput.accept(options, [], NO_CHARGED_TEXT);
  if (accepted.input === null) {
    const stats = createModelStats();
    stats.error = accepted.error;
    return stats;
  }
  return estimateModelImpl(accepted.input);
}

function estimateModelImpl(input: EngineInput): ModelStats {
  const options = input.options;
  const stats = createModelStats();
  const params = input.params;

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
  const globalCount = countTuples(params, null, options.strength);
  if (globalCount.error.code !== ErrorCode.Ok) {
    stats.error = globalCount.error;
    return stats;
  }
  stats.totalTuples = globalCount.totalTuples;
  for (const subModel of options.subModels) {
    const resolved = resolveParamNames(subModel.parameterNames, params);
    if (resolved.error.length > 0) {
      stats.error = { code: ErrorCode.InvalidInput, message: resolved.error, detail: '' };
      return stats;
    }
    const subCount = countTuples(params, resolved.indices, subModel.strength);
    if (subCount.error.code !== ErrorCode.Ok) {
      stats.error = subCount.error;
      return stats;
    }
    if (subCount.totalTuples > MAX_TUPLES - stats.totalTuples) {
      stats.error = {
        code: ErrorCode.TupleExplosion,
        message: 'Combined global and sub-model tuple count exceeds safe limit',
        detail: `limit=${MAX_TUPLES}`,
      };
      return stats;
    }
    stats.totalTuples += subCount.totalTuples;
  }

  // Estimate the suite size. When every parameter fits in one tuple the exact
  // product of the value counts is the answer; otherwise the heuristic is
  // maxValues^strength scaled by a log factor over the parameter count. The
  // scaled form is a sizing hint only -- a generated suite can fall on either
  // side of it.
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
