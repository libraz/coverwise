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
  CoverwiseError,
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

// --- Internal imports ---

import {
  estimateModel as internalEstimateModel,
  extend as internalExtend,
  generate as internalGenerate,
} from '../../src/ts/core/generator.js';
import {
  annotateClassCoverage,
  validateCoverage as internalValidateCoverage,
} from '../../src/ts/validator/coverage-validator.js';
import type {
  CoverageReport,
  ExtendInput,
  GenerateInput,
  GenerateResult,
  ModelStats,
  Parameter,
  TestCase,
} from '../types.js';

import {
  toInternalOptions,
  toInternalParams,
  toInternalTestCase,
  toPublicCoverageReport,
  toPublicModelStats,
  toPublicResult,
} from './adapter.js';

// --- Input Validation ---

function validateStrength(strength: unknown): number {
  if (strength === undefined || strength === null) {
    return 2;
  }
  if (typeof strength !== 'number' || !Number.isInteger(strength) || strength <= 0) {
    throw new Error(`Invalid strength: ${String(strength)}. Must be a positive integer.`);
  }
  return strength;
}

function validateMaxTests(maxTests: unknown): void {
  if (maxTests === undefined || maxTests === null) {
    return;
  }
  if (typeof maxTests !== 'number' || !Number.isInteger(maxTests) || maxTests < 0) {
    throw new Error(`Invalid maxTests: ${String(maxTests)}. Must be a non-negative integer.`);
  }
}

function validateSeed(seed: unknown): void {
  if (seed === undefined || seed === null) {
    return;
  }
  if (typeof seed !== 'number' || !Number.isFinite(seed)) {
    throw new Error(`Invalid seed: ${String(seed)}. Must be a finite number.`);
  }
}

function validateParameters(parameters: unknown): void {
  if (!Array.isArray(parameters) || parameters.length === 0) {
    throw new Error('Invalid parameters: must be a non-empty array.');
  }
}

function validateGenerateInput(input: GenerateInput): void {
  validateParameters(input.parameters);
  validateStrength(input.strength);
  validateMaxTests(input.maxTests);
  validateSeed(input.seed);
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
  validateGenerateInput(input);
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const result = internalGenerate(opts);
  const strength = input.strength ?? 2;

  // Annotate equivalence class coverage if applicable.
  annotateClassCoverage(result, params, strength);

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
): CoverageReport {
  validateParameters(parameters);
  const s = validateStrength(strength);
  const params = toInternalParams(parameters);
  const internalTests = tests.map((tc) => toInternalTestCase(tc, params));
  const report = internalValidateCoverage(params, internalTests, s);
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
  validateGenerateInput(input);
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const internalExisting = existing.map((tc) => toInternalTestCase(tc, params));
  const strength = input.strength ?? 2;
  const result = internalExtend(internalExisting, opts);

  annotateClassCoverage(result, params, strength);

  return toPublicResult(result, params, strength);
}

/**
 * Get model statistics without running generation.
 */
export function estimateModel(input: GenerateInput): ModelStats {
  const params = toInternalParams(input.parameters);
  const opts = toInternalOptions(input, params);
  const stats = internalEstimateModel(opts);
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
  analyzeCoverage(parameters: Parameter[], tests: TestCase[], strength?: number): CoverageReport {
    return analyzeCoverage(parameters, tests, strength);
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
