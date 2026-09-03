/// Pure TypeScript entry point for coverwise.
///
/// Provides the same API as the WASM-backed default export but runs
/// entirely in TypeScript — no WASM compilation or init() call required.

// --- Re-export public types ---

export type { Condition, ConditionStart, Constraint, IfConstraint } from '../constraint.js';
export { allOf, anyOf, not, when } from '../constraint.js';
export type {
  BoundaryParameter,
  ClassCoverage,
  CoverageReport,
  CoverwiseErrorCode,
  ExtendInput,
  FloatBoundaryParameter,
  GenerateInput,
  GenerateResult,
  GenerateStats,
  IntegerBoundaryParameter,
  ModelStats,
  NegativeCoverage,
  Parameter,
  ParameterValue,
  ParamStats,
  PlainParameter,
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
import { annotateConstraintError, parseConstraint } from '../../src/ts/model/constraint-parser.js';
import { ErrorCode, type ErrorInfo } from '../../src/ts/model/error.js';
import { validateGenerateOptions } from '../../src/ts/model/generate-options.js';
import { validateCoverage as internalValidateCoverage } from '../../src/ts/validator/coverage-validator.js';
import type {
  CoverageReport,
  ExtendInput,
  GenerateInput,
  GenerateResult,
  ModelStats,
  Parameter,
  TestCase,
} from '../types.js';
import { CoverwiseError } from '../types.js';
import {
  createStringBudget,
  type StringBudget,
  validateConstraints,
  validateExtendMode,
  validateGenerateInput,
  validateParameters,
  validateStrength,
  validateTestArray,
} from '../validation.js';

import {
  toInternalModelOptions,
  toInternalOptions,
  toInternalParams,
  toInternalTestCase,
  toPublicCoverageReport,
  toPublicError,
  toPublicModelStats,
  toPublicResult,
} from './adapter.js';

// --- Input Validation ---

// The pure surface historically throws a CoverwiseError for the numeric scalar
// checks (strength/maxTests/seed); preserve that by injecting this factory into
// the shared validators.
const pureScalarError = (message: string): Error => new CoverwiseError('INVALID_INPUT', message);

function validateInput(input: GenerateInput, budget?: StringBudget): void {
  validateGenerateInput(input, pureScalarError, budget);
}

/** Throw a CoverwiseError when the internal engine reports a structured error. */
function throwOnResultError(error: ErrorInfo): void {
  if (error.code === ErrorCode.Ok) {
    return;
  }
  throw toPublicError(error);
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
  const budget = createStringBudget();
  validateParameters(parameters, budget);
  validateTestArray(tests, 'tests', budget);
  validateConstraints(constraints, budget);
  const s = validateStrength(strength, pureScalarError);
  const params = toInternalParams(parameters);
  const internalTests = tests.map((tc) => toInternalTestCase(tc, params));

  // The model reaching the validator has been through the same acceptance gate
  // as the one reaching the generator — the documented budgets are limits on
  // the input, not on which function received it. The requested strength is
  // deliberately not submitted: see toInternalModelOptions.
  throwOnResultError(validateGenerateOptions(toInternalModelOptions(params, constraints ?? [])));

  // Parse optional constraint expressions.
  const parsedConstraints: ConstraintNode[] = [];
  if (constraints && constraints.length > 0) {
    for (const expr of constraints) {
      const parseResult = parseConstraint(expr, params);
      if (parseResult.error.code !== ErrorCode.Ok || !parseResult.constraint) {
        const annotated = annotateConstraintError(expr, parseResult.error);
        // A parse that failed without classifying itself is still a constraint
        // failure; that is the one code decided here, and only because the
        // engine reported none. Everything else crosses through the single
        // conversion, which keeps code, message and detail as they arrived.
        throw toPublicError(
          annotated.code === ErrorCode.Ok
            ? { ...annotated, code: ErrorCode.ConstraintError }
            : annotated,
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
  const budget = createStringBudget();
  validateTestArray(existing, 'existing', budget);
  validateInput(input, budget);
  validateExtendMode(input.mode);
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const internalExisting = existing.map((tc) => toInternalTestCase(tc, params, true));
  const strength = input.strength ?? 2;
  const result = internalExtend(internalExisting, opts);
  throwOnResultError(result.error);

  return toPublicResult(result, params, strength, existing);
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
  // Constructed through create() only, matching the WASM surface so a program
  // can move between the two entry points by swapping the import specifier.
  private constructor() {}

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
