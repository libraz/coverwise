/// Public type definitions for the coverwise API.

export interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;
  aliases?: string[];
  /**
   * Equivalence class name. Values sharing a class are interchangeable for the
   * class-coverage metric; populate {@link GenerateResult.classCoverage}.
   */
  class?: string;
}

export interface Parameter {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
  /**
   * Boundary value expansion type. When set with {@link Parameter.range}, the
   * value set is expanded with min/max boundary values (min-1, min, min+1,
   * max-1, max, max+1 for `'integer'`; the same spaced by {@link Parameter.step}
   * for `'float'`).
   */
  type?: 'integer' | 'float';
  /** Inclusive `[min, max]` range driving boundary value expansion. */
  range?: [number, number];
  /** Step size for `'float'` boundary expansion (default 1.0). */
  step?: number;
}

export interface SubModel {
  parameters: string[];
  strength: number;
}

export interface WeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}

export interface GenerateInput {
  parameters: Parameter[];
  constraints?: string[];
  strength?: number;
  seed?: number;
  weights?: WeightConfig;
  seeds?: TestCase[];
  maxTests?: number;
  subModels?: SubModel[];
}

/**
 * A test case as a readable key-value map.
 * e.g. { os: "win", browser: "chrome", arch: "x64" }
 */
export interface TestCase {
  [parameterName: string]: string | number | boolean;
}

/**
 * A human-readable uncovered tuple with context.
 */
export interface UncoveredTuple {
  /** e.g. ["os=win", "browser=safari"] */
  tuple: string[];
  /** Parameter names involved. */
  params: string[];
  /**
   * Why this tuple is uncovered.
   *
   * Currently always `"never covered"` — tuples that are impossible due to
   * constraints are removed from the coverage universe entirely (they do not
   * appear here and are not counted in `totalTuples`), matching the
   * generator's `excludeInvalidTuples` semantics.
   */
  reason: string;
  /** Human-readable display string. */
  display: string;
}

/**
 * Statistics for evaluation and comparison.
 */
export interface GenerateStats {
  totalTuples: number;
  coveredTuples: number;
  testCount: number;
}

export interface GenerateResult {
  tests: TestCase[];
  negativeTests: TestCase[];
  coverage: number;
  /** Uncovered tuples with context. Empty when coverage is 1.0. */
  uncovered: UncoveredTuple[];
  /** Statistics for evaluation and comparison. */
  stats: GenerateStats;
  /** Actionable suggestions with proposed test cases. */
  suggestions: Array<{ description: string; testCase: Record<string, string> }>;
  /** Warnings (e.g. performance degradation). */
  warnings: string[];
  /** Strength used for generation. */
  strength: number;
  /** Equivalence class coverage (present when parameters have equivalence classes). */
  classCoverage?: {
    totalClassTuples: number;
    coveredClassTuples: number;
    classCoverageRatio: number;
  };
}

export interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;
  /** Every uncovered tuple with context. */
  uncovered: UncoveredTuple[];
}

export interface ExtendInput extends GenerateInput {
  /** How to handle existing tests. */
  mode?: 'strict';
}

export interface ModelStats {
  parameterCount: number;
  totalValues: number;
  strength: number;
  totalTuples: number;
  estimatedTests: number;
  subModelCount: number;
  constraintCount: number;
  parameters: Array<{
    name: string;
    valueCount: number;
    invalidCount: number;
  }>;
}

/**
 * Canonical string error codes. The values mirror the C++ `model::Error::Code`
 * enum (error.h) one-to-one; see {@link errorCodeFromNumber} for the mapping.
 */
export type CoverwiseErrorCode =
  | 'CONSTRAINT_ERROR'
  | 'INSUFFICIENT_COVERAGE'
  | 'INVALID_INPUT'
  | 'TUPLE_EXPLOSION';

/**
 * Structured error thrown by every coverwise surface (WASM-backed and pure).
 *
 * Extends the native `Error`, so `instanceof Error` holds, a stack trace is
 * captured, and error-reporting tools (Sentry, etc.) treat it as a real error.
 * The typed string `code` lets callers branch reliably:
 *
 * @example
 * try {
 *   generate(input);
 * } catch (e) {
 *   if (e instanceof CoverwiseError && e.code === 'CONSTRAINT_ERROR') { ... }
 * }
 */
export class CoverwiseError extends Error {
  /** Typed, surface-independent error category. */
  readonly code: CoverwiseErrorCode;
  /** Optional secondary context (e.g. the offending fragment). */
  readonly detail?: string;

  constructor(code: CoverwiseErrorCode, message: string, detail?: string) {
    super(message);
    this.name = 'CoverwiseError';
    this.code = code;
    this.detail = detail;
    // Restore the prototype chain when targeting ES5-style transpilation so
    // `instanceof CoverwiseError` keeps working after `super()`.
    Object.setPrototypeOf(this, CoverwiseError.prototype);
  }
}

/**
 * Map a numeric error code (from the WASM module or `model::Error::Code`) to its
 * canonical string code. Values: 1=CONSTRAINT_ERROR, 2=INSUFFICIENT_COVERAGE,
 * 3=INVALID_INPUT, 4=TUPLE_EXPLOSION. Anything else falls back to INVALID_INPUT.
 */
export function errorCodeFromNumber(code: number | undefined): CoverwiseErrorCode {
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

/** Per-parameter statistics. */
export interface ParamStats {
  name: string;
  valueCount: number;
  invalidCount: number;
}

/** Equivalence class coverage metrics. */
export interface ClassCoverage {
  totalClassTuples: number;
  coveredClassTuples: number;
  classCoverageRatio: number;
}

/** Actionable suggestion with a proposed test case. */
export interface Suggestion {
  description: string;
  testCase: Record<string, string>;
}
