/// Adapter between public API types and internal TS engine types.

import { type BoundaryConfig, BoundaryType } from '../../src/ts/model/boundary.js';
import { boundaryShapeError, DEFAULT_BOUNDARY_STEP } from '../../src/ts/model/boundary-rules.js';
import { ErrorCode, type ErrorInfo } from '../../src/ts/model/error.js';
import type { ModelStats as InternalModelStats } from '../../src/ts/model/generate-options.js';
import {
  createGenerateOptions,
  createWeightConfig,
  expandBoundaries,
  type GenerateOptions as InternalGenerateOptions,
} from '../../src/ts/model/generate-options.js';
import {
  Parameter as InternalParameter,
  resolveValueName,
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
import { CoverwiseError, errorCodeFromNumber } from '../types.js';

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
 * Convert an internal ErrorInfo into the public error type.
 *
 * The one place a pure-surface failure becomes public, and it carries all three
 * of what the engine reported: the code as the engine classified it, and the
 * message and detail as two fields rather than one string. Naming a code here
 * instead of mapping the one that arrived would relabel a failure the caller is
 * meant to branch on, and joining the halves would decide how the failure reads
 * — that belongs to whoever renders it, which is why the WASM surface hands the
 * same three across unchanged.
 */
export function toPublicError(error: ErrorInfo): CoverwiseError {
  return new CoverwiseError(
    errorCodeFromNumber(error.code),
    error.message,
    error.detail || undefined,
  );
}

/**
 * Convert public Parameter[] to internal Parameter[].
 *
 * Normalizes all values to strings, extracts invalid flags and aliases
 * from ParameterValue objects.
 */
export function toInternalParams(params: PublicParameter[]): InternalParameter[] {
  const result: InternalParameter[] = [];
  const boundaryConfigs: Record<string, BoundaryConfig> = Object.create(null);

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

    const param = new InternalParameter(pub.name, values);
    if (hasInvalid) {
      param.setInvalid(invalidFlags);
    }
    if (hasAliases) {
      param.setAliases(aliases);
    }
    if (hasClasses) {
      param.setEquivalenceClasses(eqClasses);
    }

    const bc = boundaryConfigFromParam(pub);
    if (bc) {
      boundaryConfigs[param.name] = bc;
    }
    result.push(param);
  }

  // Expansion runs before the parameter set is judged, so the rules apply to the
  // value space the engine will use — a boundary parameter whose only
  // spelled-out value is an invalid sentinel is well-formed, because expansion
  // is about to supply the valid ones, and a generated value that collides with
  // a retained alias is caught here rather than reaching the engine. Expansion
  // carries per-value metadata across by value identity, so aliases and classes
  // on the values it keeps survive.
  const expansion = expandBoundaries(result, boundaryConfigs);
  if (expansion.error.code !== ErrorCode.Ok) {
    throw toPublicError(expansion.error);
  }

  // Semantic checks shared with the WASM/CLI surfaces (duplicate names/values,
  // empty values). Kept here so every pure-JS entry point inherits them.
  const semanticError = validateInternalParameters(expansion.params);
  if (semanticError.length > 0) {
    throw new CoverwiseError('INVALID_INPUT', semanticError);
  }

  return expansion.params;
}

/**
 * Convert a public TestCase (key-value map) to an internal TestCase (index array).
 *
 * @param tc - The row to convert.
 * @param params - Parameters whose value lists resolve the row's values.
 * @param allowUnknown - How a value outside the parameter's domain is treated.
 *   Defaults to `true`, the rule for recorded rows (`tests`, `existing`): the
 *   position is left unassigned and the coverage validator reports the row,
 *   rather than one drifted row failing the whole call. Callers that assert the
 *   row really is a test case for this model — `seeds` — pass `false`.
 */
export function toInternalTestCase(
  tc: PublicTestCase,
  params: InternalParameter[],
  allowUnknown = true,
): InternalTestCase {
  const values: number[] = new Array(params.length).fill(UNASSIGNED);
  let unresolved: string[] | undefined;
  for (let i = 0; i < params.length; ++i) {
    const paramName = params[i].name;
    if (Object.hasOwn(tc, paramName)) {
      const valStr = valueToString(tc[paramName]);
      const idx = resolveValueName(params[i], valStr);
      if (idx === UNASSIGNED) {
        if (allowUnknown) {
          // Filled on first drift only: a row that matches the model costs
          // nothing, and a row that does not keeps the caller's own text so the
          // diagnostic can name it instead of an internal index.
          unresolved ??= new Array(params.length).fill('');
          unresolved[i] = valStr;
          continue;
        }
        throw new CoverwiseError(
          'INVALID_INPUT',
          `Unknown value '${valStr}' for parameter '${paramName}'`,
        );
      }
      values[i] = idx;
    }
  }
  return unresolved ? { values, unresolved } : { values };
}

/**
 * Convert expanded internal parameters into GenerateOptions entries.
 *
 * Aliases and equivalence classes are threaded onto the entries so the engine
 * (which rebuilds Parameter objects from opts.parameters) honors them in
 * constraint resolution and class coverage.
 */
function toOptionParameters(params: InternalParameter[]): InternalGenerateOptions['parameters'] {
  return params.map((p) => ({
    name: p.name,
    values: p.values,
    ...(p.hasInvalidValues ? { invalid: p.invalid } : {}),
    ...(p.hasAliases ? { aliases: p.allAliases } : {}),
    ...(p.hasEquivalenceClasses ? { equivalenceClasses: p.equivalenceClasses } : {}),
  }));
}

/**
 * Build the options that submit a model — parameters and constraints, no suite
 * — to the acceptance gate.
 *
 * The analysis strength is not a property of the model: a suite may be analyzed
 * at a strength above the parameter count, where the tuple universe is simply
 * empty. Strength 1 therefore stands in here so the gate judges the model
 * alone, and the caller's strength goes to the validator instead.
 */
export function toInternalModelOptions(
  params: InternalParameter[],
  constraints: string[],
): InternalGenerateOptions {
  return createGenerateOptions({
    parameters: toOptionParameters(params),
    constraintExpressions: constraints,
    strength: 1,
  });
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

  // A seed is asserted to be a real test case for this model, so a value outside
  // the domain is an error rather than an unassigned position.
  const seeds = (input.seeds ?? []).map((tc) => toInternalTestCase(tc, params, false));

  // Boundary expansion is already applied in toInternalParams, so `params` is
  // the final value space. boundaryConfigs is intentionally left empty (no
  // double-expansion).
  return createGenerateOptions({
    parameters: toOptionParameters(params),
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
 * parameter carries no boundary fields at all.
 *
 * Shape only, on every exit: whether the JS value can be converted into a
 * BoundaryConfig at all. A parameter opts in by carrying any of `type` /
 * `range` / `step`; having opted in it must supply a `type` of 'integer' or
 * 'float' and a 2-element numeric `range` ([min, max]), with an optional
 * numeric `step`. A malformed shape is an error rather than an opt-out —
 * degrading to 'no expansion' would generate over a value space the caller
 * never described.
 *
 * Whether the range is ordered and finite, whether the step is positive or one
 * an integer expansion can honor, whether the endpoints are safe integers, and
 * whether the six generated values are finite are not decided here: those are
 * acceptance rules and validateBoundaryConfigs in the model layer is the only
 * thing that applies them. Repeating one of them here would let this surface
 * accept or reject models the WASM binding and the CLI do not. `step` is
 * therefore carried through unjudged for both types, including integer.
 */
function boundaryConfigFromParam(p: PublicParameter): BoundaryConfig | null {
  // Public TypeScript callers can only construct the union in `types.ts`, but
  // JavaScript callers may still pass arbitrary objects at runtime.
  const raw = p as PublicParameter & { type?: unknown; range?: unknown; step?: unknown };
  if (raw.type === undefined && raw.range === undefined && raw.step === undefined) {
    return null;
  }
  if (raw.type !== 'integer' && raw.type !== 'float') {
    throw new CoverwiseError('INVALID_INPUT', boundaryShapeError.type(p.name));
  }
  const range = raw.range;
  if (
    !Array.isArray(range) ||
    range.length !== 2 ||
    typeof range[0] !== 'number' ||
    typeof range[1] !== 'number'
  ) {
    throw new CoverwiseError('INVALID_INPUT', boundaryShapeError.range(p.name));
  }
  const step = raw.step ?? DEFAULT_BOUNDARY_STEP;
  if (typeof step !== 'number') {
    throw new CoverwiseError('INVALID_INPUT', boundaryShapeError.step(p.name));
  }
  return {
    type: raw.type === 'integer' ? BoundaryType.Integer : BoundaryType.Float,
    minValue: range[0],
    maxValue: range[1],
    step,
  };
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
  const result = Object.create(null) as PublicTestCase;
  for (let i = 0; i < params.length && i < tc.values.length; ++i) {
    if (tc.values[i] !== UNASSIGNED && tc.values[i] >= 0 && tc.values[i] < params[i].size) {
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
    indices: ut.indices ?? [],
    reason: ut.reason,
    display: internalUncoveredToString(ut),
  };
}

/**
 * Convert an internal GenerateResult to a public GenerateResult.
 *
 * @param preservedRows Rows the caller handed to extend, or undefined. Extend
 *   keeps them exactly as supplied, so they are echoed rather than rendered
 *   from value indices — rendering would substitute the primary value for an
 *   alias the caller wrote, and drop members the model no longer declares.
 *   Doing it here, where a result becomes public, is what keeps the rule from
 *   having to be re-applied by each entry point.
 */
export function toPublicResult(
  result: InternalGenerateResult,
  params: InternalParameter[],
  strength: number,
  preservedRows?: readonly PublicTestCase[],
): PublicGenerateResult {
  const preservedCount = preservedRows?.length ?? 0;
  const tests = result.tests.map((tc, i) =>
    i < preservedCount
      ? (preservedRows as readonly PublicTestCase[])[i]
      : toPublicTestCase(tc, params, i),
  );
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
    uncoveredCount: result.uncoveredCount,
    omittedUncovered: result.omittedUncovered,
    stats: {
      totalTuples: result.stats.totalTuples,
      coveredTuples: result.stats.coveredTuples,
      testCount: result.stats.testCount,
    },
    suggestions,
    warnings: result.warnings,
    strength,
  };

  if (result.negativeCoverage) {
    pubResult.negativeCoverage = {
      totalTuples: result.negativeCoverage.totalTuples,
      coveredTuples: result.negativeCoverage.coveredTuples,
      omittedTuples: result.negativeCoverage.omittedTuples,
      coverageRatio: result.negativeCoverage.coverageRatio,
    };
  }

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
    uncoveredCount: report.uncoveredCount,
    omittedUncovered: report.omittedUncovered,
    invalidTests: report.invalidTests,
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
