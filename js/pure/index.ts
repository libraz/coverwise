/// Pure TypeScript entry point for coverwise.
///
/// Provides the same API as the WASM-backed default export but runs
/// entirely in TypeScript — no WASM compilation or init() call required.

// --- Re-export public types ---

export type { Condition, ConditionStart, Constraint } from '../constraint.js';
export { allOf, anyOf, not, when } from '../constraint.js';
export type {
  ClassCoverage,
  CoverageReport,
  CoverwiseErrorCode,
  ExtendInput,
  GenerateInput,
  GenerateResult,
  GenerateStats,
  ModelStats,
  Parameter,
  ParameterValue,
  ParamStats,
  SubModel,
  Suggestion,
  TestCase,
  UncoveredTuple,
  WeightConfig,
} from '../types.js';
export { CoverwiseError } from '../types.js';

// --- Internal imports ---

import {
  estimateModel as internalEstimateModel,
  extend as internalExtend,
  generate as internalGenerate,
} from '../../src/ts/core/generator.js';
import type { ConstraintNode } from '../../src/ts/model/constraint-ast.js';
import { parseConstraint } from '../../src/ts/model/constraint-parser.js';
import { validateCoverage as internalValidateCoverage } from '../../src/ts/validator/coverage-validator.js';
import type {
  CoverageReport,
  CoverwiseErrorCode,
  ExtendInput,
  GenerateInput,
  GenerateResult,
  ModelStats,
  Parameter,
  TestCase,
} from '../types.js';
import { CoverwiseError } from '../types.js';
import {
  validateConstraints,
  validateExtendMode,
  validateGenerateInput,
  validateParameters,
  validateStrength,
  validateTestArray,
} from '../validation.js';

import {
  toInternalOptions,
  toInternalParams,
  toInternalTestCase,
  toPublicCoverageReport,
  toPublicModelStats,
  toPublicResult,
} from './adapter.js';

// --- Input Validation ---

// The pure surface historically throws a CoverwiseError for the numeric scalar
// checks (strength/maxTests/seed); preserve that by injecting this factory into
// the shared validators.
const pureScalarError = (message: string): Error => new CoverwiseError('INVALID_INPUT', message);

function validateInput(input: GenerateInput): void {
  validateGenerateInput(input, pureScalarError);
}

/**
 * Map the numeric internal {@link ErrorCode} to its canonical string code.
 * Mirrors errorCodeFromNumber in ../types.js (kept local to avoid importing a
 * numeric-mapping helper that the public surface does not otherwise need).
 */
function toErrorCode(code: number): CoverwiseErrorCode {
  switch (code) {
    case 1:
      return 'CONSTRAINT_ERROR';
    case 2:
      return 'INSUFFICIENT_COVERAGE';
    case 4:
      return 'TUPLE_EXPLOSION';
    default:
      return 'INVALID_INPUT';
  }
}

/** Throw a CoverwiseError when the internal engine reports a structured error. */
function throwOnResultError(error: { code: number; message: string; detail: string }): void {
  if (error.code === 0) {
    return;
  }
  const message = error.detail ? `${error.message}: ${error.detail}` : error.message;
  throw new CoverwiseError(toErrorCode(error.code), message);
}

// --- Core API ---

/**
 * Generate a covering array. One function, sensible defaults.
 *
 * Unlike the WASM version, this is fully synchronous and requires no init() call.
 *
 * @example
 * const result = generate({
 *   parameters: [
 *     { name: "os", values: ["win", "mac", "linux"] },
 *     { name: "browser", values: ["chrome", "firefox", "safari"] },
 *   ],
 * });
 * // result.tests: [{ os: "win", browser: "chrome" }, ...]
 * // result.coverage: 1.0
 */
export function generate(input: GenerateInput): GenerateResult {
  validateInput(input);
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const result = internalGenerate(opts);
  // Core reports early-exit failures (e.g. constraint parse errors) via
  // result.error rather than throwing; surface them as CoverwiseError.
  throwOnResultError(result.error);
  const strength = input.strength ?? 2;

  return toPublicResult(result, params, strength);
}

/**
 * Analyze t-wise coverage of an existing test suite.
 * Use this to check coverage of manually written tests.
 *
 * @example
 * const report = analyzeCoverage(
 *   [{ name: "os", values: ["win", "mac"] }, ...],
 *   [{ os: "win", browser: "chrome" }, ...],
 * );
 * // report.uncovered: [{ tuple: ["os=mac", "browser=chrome"], ... }]
 */
export function analyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,
  constraints?: string[],
): CoverageReport {
  validateParameters(parameters);
  validateTestArray(tests, 'tests');
  validateConstraints(constraints);
  const s = validateStrength(strength, pureScalarError);
  const params = toInternalParams(parameters);
  const internalTests = tests.map((tc) => toInternalTestCase(tc, params));

  // Parse optional constraint expressions.
  const parsedConstraints: ConstraintNode[] = [];
  if (constraints && constraints.length > 0) {
    for (const expr of constraints) {
      const parseResult = parseConstraint(expr, params);
      if (parseResult.error.code !== 0 || !parseResult.constraint) {
        throw new CoverwiseError(
          toErrorCode(parseResult.error.code),
          `Invalid constraint "${expr}": ${parseResult.error.message}${
            parseResult.error.detail ? ` — ${parseResult.error.detail}` : ''
          }`,
        );
      }
      parsedConstraints.push(parseResult.constraint);
    }
  }

  const report = internalValidateCoverage(params, internalTests, s, parsedConstraints);
  throwOnResultError(report.error);
  const result = toPublicCoverageReport(report);
  // When there are no tuples, coverage is vacuously 1.0.
  if (result.totalTuples === 0) {
    result.coverageRatio = 1.0;
  }
  return result;
}

/**
 * Extend an existing test suite with additional tests to improve coverage.
 *
 * mode: "strict" (default) keeps existing tests as-is.
 * Only "strict" mode is supported (existing tests are kept as-is).
 */
export function extendTests(existing: TestCase[], input: ExtendInput): GenerateResult {
  validateTestArray(existing, 'existing');
  validateInput(input);
  validateExtendMode(input.mode);
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const internalExisting = existing.map((tc) => toInternalTestCase(tc, params, true));
  const strength = input.strength ?? 2;
  const result = internalExtend(internalExisting, opts);
  throwOnResultError(result.error);

  const publicResult = toPublicResult(result, params, strength);
  for (let i = 0; i < existing.length; ++i) {
    publicResult.tests[i] = existing[i];
  }
  return publicResult;
}

/**
 * Get model statistics without running generation.
 */
export function estimateModel(input: GenerateInput): ModelStats {
  validateInput(input);
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const stats = internalEstimateModel(opts);
  throwOnResultError(stats.error);
  return toPublicModelStats(stats);
}

// --- Class-based API ---

/**
 * Class-based wrapper around the coverwise API.
 * Provides the same functionality as the free functions in an object-oriented style.
 *
 * Unlike the WASM version, create() is synchronous (returned as a resolved Promise
 * for API compatibility).
 *
 * @example
 * const cw = await Coverwise.create();
 * const result = cw.generate({ parameters: [...] });
 */
export class Coverwise {
  /**
   * Create a Coverwise instance.
   * Returns immediately (no WASM loading needed).
   */
  static async create(): Promise<Coverwise> {
    return new Coverwise();
  }

  /**
   * Generate a covering array. One function, sensible defaults.
   */
  generate(input: GenerateInput): GenerateResult {
    return generate(input);
  }

  /**
   * Analyze t-wise coverage of an existing test suite.
   */
  analyzeCoverage(
    parameters: Parameter[],
    tests: TestCase[],
    strength?: number,
    constraints?: string[],
  ): CoverageReport {
    return analyzeCoverage(parameters, tests, strength, constraints);
  }

  /**
   * Extend an existing test suite with additional tests to improve coverage.
   */
  extendTests(existing: TestCase[], input: ExtendInput): GenerateResult {
    return extendTests(existing, input);
  }

  /**
   * Get model statistics without running generation.
   */
  estimateModel(input: GenerateInput): ModelStats {
    return estimateModel(input);
  }
}

/**
 * No-op init for backward compatibility.
 * The pure TS version requires no initialization.
 */
export async function init(): Promise<void> {}
