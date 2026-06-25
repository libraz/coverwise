/// Adapter between public API types and internal TS engine types.

import {
  type BoundaryConfig,
  BoundaryType,
  expandBoundaryValues,
} from '../../src/ts/model/boundary.js';
import type { ModelStats as InternalModelStats } from '../../src/ts/model/generate-options.js';
import {
  createGenerateOptions,
  createWeightConfig,
  type GenerateOptions as InternalGenerateOptions,
} from '../../src/ts/model/generate-options.js';
import {
  Parameter as InternalParameter,
  UNASSIGNED,
  validateParameters as validateInternalParameters,
} from '../../src/ts/model/parameter.js';
import type {
  GenerateResult as InternalGenerateResult,
  TestCase as InternalTestCase,
  UncoveredTuple as InternalUncoveredTuple,
} from '../../src/ts/model/test-case.js';
import { uncoveredTupleToString as internalUncoveredToString } from '../../src/ts/model/test-case.js';
import type { CoverageReport as InternalCoverageReport } from '../../src/ts/validator/coverage-validator.js';
import type {
  GenerateInput,
  ParameterValue,
  CoverageReport as PublicCoverageReport,
  GenerateResult as PublicGenerateResult,
  ModelStats as PublicModelStats,
  Parameter as PublicParameter,
  TestCase as PublicTestCase,
  UncoveredTuple as PublicUncoveredTuple,
} from '../types.js';
import { CoverwiseError } from '../types.js';

/**
 * Convert a JS value (string, number, or boolean) to a string representation.
 *
 * Numbers use Number.prototype.toString() (the canonical cross-surface rule):
 * the C++ JsNumberToString and the WASM binding reproduce this exactly, so a
 * numeric value renders to a byte-identical string on every surface.
 * - string -> as-is
 * - number -> JS toString() ("42", "3.14", "1e-7")
 * - boolean -> "true" / "false"
 */
function valueToString(v: string | number | boolean): string {
  if (typeof v === 'string') {
    return v;
  }
  if (typeof v === 'boolean') {
    return v ? 'true' : 'false';
  }
  // number — Number.prototype.toString() is the canonical formatting.
  return v.toString();
}

/**
 * Convert public Parameter[] to internal Parameter[].
 *
 * Normalizes all values to strings, extracts invalid flags and aliases
 * from ParameterValue objects.
 */
export function toInternalParams(params: PublicParameter[]): InternalParameter[] {
  const result: InternalParameter[] = [];

  for (const pub of params) {
    // Shape guards mirroring the WASM binding: reject a non-string name or a
    // non-array `values` BEFORE iterating, so a string `values:'win'` is never
    // walked character-by-character (silent data corruption).
    if (typeof pub?.name !== 'string') {
      throw new CoverwiseError('INVALID_INPUT', 'Parameter name must be a non-empty string');
    }
    if (!Array.isArray(pub.values)) {
      throw new CoverwiseError(
        'INVALID_INPUT',
        `Parameter '${pub.name}' must have at least one value`,
      );
    }

    const values: string[] = [];
    const invalidFlags: boolean[] = [];
    const aliases: string[][] = [];
    const eqClasses: string[] = [];
    let hasInvalid = false;
    let hasAliases = false;
    let hasClasses = false;

    for (const item of pub.values) {
      if (typeof item === 'object' && item !== null && 'value' in item) {
        // ParameterValue object form
        const pv = item as ParameterValue;
        values.push(valueToString(pv.value));
        const isInvalid = pv.invalid ?? false;
        invalidFlags.push(isInvalid);
        if (isInvalid) {
          hasInvalid = true;
        }
        const valueAliases = pv.aliases ?? [];
        aliases.push(valueAliases);
        if (valueAliases.length > 0) {
          hasAliases = true;
        }
        const eqClass = pv.class ?? '';
        eqClasses.push(eqClass);
        if (eqClass.length > 0) {
          hasClasses = true;
        }
      } else {
        // Scalar value
        values.push(valueToString(item as string | number | boolean));
        invalidFlags.push(false);
        aliases.push([]);
        eqClasses.push('');
      }
    }

    let param = new InternalParameter(pub.name, values);
    if (hasInvalid) {
      param.setInvalid(invalidFlags);
    }
    if (hasAliases) {
      param.setAliases(aliases);
    }
    if (hasClasses) {
      param.setEquivalenceClasses(eqClasses);
    }

    // Apply boundary value expansion up front, so the resulting Parameter is the
    // single source of truth used for generation AND rendering. (Expansion
    // regenerates the value set, so aliases/classes are intentionally dropped,
    // matching expandBoundaryValues.)
    const bc = boundaryConfigFromParam(pub);
    if (bc) {
      param = expandBoundaryValues(param, bc);
    }
    result.push(param);
  }

  // Semantic checks shared with the WASM/CLI surfaces (duplicate names/values,
  // empty values). Kept here so every pure-JS entry point inherits them.
  const semanticError = validateInternalParameters(result);
  if (semanticError.length > 0) {
    throw new CoverwiseError('INVALID_INPUT', semanticError);
  }

  return result;
}

/**
 * Convert a public TestCase (key-value map) to an internal TestCase (index array).
 */
export function toInternalTestCase(
  tc: PublicTestCase,
  params: InternalParameter[],
): InternalTestCase {
  const values: number[] = new Array(params.length).fill(UNASSIGNED);
  for (let i = 0; i < params.length; ++i) {
    const paramName = params[i].name;
    if (Object.hasOwn(tc, paramName)) {
      const valStr = valueToString(tc[paramName]);
      const idx = params[i].findValueIndex(valStr);
      if (idx === UNASSIGNED) {
        throw new Error(`Unknown value '${valStr}' for parameter '${paramName}'`);
      }
      values[i] = idx;
    }
  }
  return { values };
}

/**
 * Convert a full GenerateInput to internal GenerateOptions.
 */
export function toInternalOptions(
  input: GenerateInput,
  params: InternalParameter[],
): InternalGenerateOptions {
  const weights = createWeightConfig();
  if (input.weights) {
    for (const paramName of Object.keys(input.weights)) {
      if (!Object.hasOwn(input.weights, paramName)) {
        continue;
      }
      const paramWeights = input.weights[paramName];
      const inner: Record<string, number> = Object.create(null);
      for (const valueName of Object.keys(paramWeights)) {
        if (!Object.hasOwn(paramWeights, valueName)) {
          continue;
        }
        inner[valueName] = paramWeights[valueName];
      }
      weights.entries[paramName] = inner;
    }
  }

  const subModels = (input.subModels ?? []).map((sm) => ({
    parameterNames: sm.parameters,
    strength: sm.strength,
  }));

  const seeds = (input.seeds ?? []).map((tc) => toInternalTestCase(tc, params));

  // Boundary expansion is already applied in toInternalParams, so `params` is
  // the final value space. Aliases/equivalence classes are threaded onto the
  // options' parameter entries so the engine (which rebuilds Parameter objects
  // from opts.parameters) honors them in constraint resolution and class
  // coverage. boundaryConfigs is intentionally left empty (no double-expansion).
  return createGenerateOptions({
    parameters: params.map((p) => ({
      name: p.name,
      values: p.values,
      ...(p.hasInvalidValues ? { invalid: p.invalid } : {}),
      ...(p.hasAliases ? { aliases: p.allAliases } : {}),
      ...(p.hasEquivalenceClasses ? { equivalenceClasses: p.equivalenceClasses } : {}),
    })),
    constraintExpressions: input.constraints ?? [],
    strength: input.strength ?? 2,
    seed: input.seed ?? 0,
    maxTests: input.maxTests ?? 0,
    seeds,
    subModels,
    weights,
  });
}

/**
 * Derive a BoundaryConfig from a public parameter object, or null when the
 * parameter does not opt into boundary expansion.
 *
 * A parameter participates when it carries a `type` ('integer' | 'float') and a
 * 2-element numeric `range` ([min, max]); for 'float' an optional `step`
 * (default 1.0) sets the spacing. Mirrors the WASM ParseBoundaryConfigs and the
 * CLI's canonical shape so every surface expands the same value set.
 */
function boundaryConfigFromParam(p: PublicParameter): BoundaryConfig | null {
  if (typeof p.type !== 'string') {
    return null;
  }
  const range = p.range;
  if (
    !Array.isArray(range) ||
    range.length !== 2 ||
    typeof range[0] !== 'number' ||
    typeof range[1] !== 'number'
  ) {
    return null;
  }
  if (p.type === 'integer') {
    return { type: BoundaryType.Integer, minValue: range[0], maxValue: range[1], step: 1.0 };
  }
  if (p.type === 'float') {
    return {
      type: BoundaryType.Float,
      minValue: range[0],
      maxValue: range[1],
      step: typeof p.step === 'number' ? p.step : 1.0,
    };
  }
  return null;
}

/**
 * Convert an internal TestCase (index array) to a public TestCase (key-value map).
 *
 * Uses displayName for alias rotation, matching the WASM TestCaseToJS behavior.
 */
export function toPublicTestCase(
  tc: InternalTestCase,
  params: InternalParameter[],
  rotation: number,
): PublicTestCase {
  const result: PublicTestCase = {};
  for (let i = 0; i < params.length && i < tc.values.length; ++i) {
    if (tc.values[i] !== UNASSIGNED) {
      result[params[i].name] = params[i].displayName(tc.values[i], rotation);
    }
  }
  return result;
}

/**
 * Convert an internal UncoveredTuple to a public UncoveredTuple (with display string).
 */
function toPublicUncoveredTuple(ut: InternalUncoveredTuple): PublicUncoveredTuple {
  return {
    tuple: ut.tuple,
    params: ut.params,
    reason: ut.reason,
    display: internalUncoveredToString(ut),
  };
}

/**
 * Convert an internal GenerateResult to a public GenerateResult.
 */
export function toPublicResult(
  result: InternalGenerateResult,
  params: InternalParameter[],
  strength: number,
): PublicGenerateResult {
  const tests = result.tests.map((tc, i) => toPublicTestCase(tc, params, i));
  const negativeTests = result.negativeTests.map((tc, i) => toPublicTestCase(tc, params, i));
  const uncovered = result.uncovered.map(toPublicUncoveredTuple);

  const suggestions = result.suggestions.map((s) => ({
    description: s.description,
    testCase: toPublicTestCase(s.testCase, params, 0) as Record<string, string>,
  }));

  const pubResult: PublicGenerateResult = {
    tests,
    negativeTests,
    coverage: result.coverage,
    uncovered,
    stats: {
      totalTuples: result.stats.totalTuples,
      coveredTuples: result.stats.coveredTuples,
      testCount: result.stats.testCount,
    },
    suggestions,
    warnings: result.warnings,
    strength,
  };

  if (result.classCoverage) {
    pubResult.classCoverage = {
      totalClassTuples: result.classCoverage.totalClassTuples,
      coveredClassTuples: result.classCoverage.coveredClassTuples,
      classCoverageRatio: result.classCoverage.classCoverageRatio,
    };
  }

  return pubResult;
}

/**
 * Convert an internal CoverageReport to a public CoverageReport.
 */
export function toPublicCoverageReport(report: InternalCoverageReport): PublicCoverageReport {
  return {
    totalTuples: report.totalTuples,
    coveredTuples: report.coveredTuples,
    coverageRatio: report.coverageRatio,
    uncovered: report.uncovered.map(toPublicUncoveredTuple),
  };
}

/**
 * Convert an internal ModelStats to a public ModelStats.
 */
export function toPublicModelStats(stats: InternalModelStats): PublicModelStats {
  return {
    parameterCount: stats.parameterCount,
    totalValues: stats.totalValues,
    strength: stats.strength,
    totalTuples: stats.totalTuples,
    estimatedTests: stats.estimatedTests,
    subModelCount: stats.subModelCount,
    constraintCount: stats.constraintCount,
    parameters: stats.parameters.map((p) => ({
      name: p.name,
      valueCount: p.valueCount,
      invalidCount: p.invalidCount,
    })),
  };
}
