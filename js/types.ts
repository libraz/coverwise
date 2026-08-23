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

interface ParameterBase {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

/** A normal discrete parameter with no boundary-expansion fields. */
export interface PlainParameter extends ParameterBase {
  type?: never;
  range?: never;
  step?: never;
}

/** Integer boundary expansion; the step is fixed at 1 when present. */
export interface IntegerBoundaryParameter extends ParameterBase {
  type: 'integer';
  range: [number, number];
  step?: 1;
}

/** Floating-point boundary expansion. */
export interface FloatBoundaryParameter extends ParameterBase {
  type: 'float';
  range: [number, number];
  step?: number;
}

/** Public parameter contract. Boundary fields are an all-or-nothing union. */
export type Parameter = PlainParameter | IntegerBoundaryParameter | FloatBoundaryParameter;

/**
 * @deprecated Use {@link IntegerBoundaryParameter} or {@link FloatBoundaryParameter}.
 */
export type BoundaryParameter = IntegerBoundaryParameter | FloatBoundaryParameter;

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
  /** Exact `(parameterIndex, valueIndex)` pairs; safe even when text contains `=`. */
  indices: Array<[number, number]>;
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

/** Coverage metrics for single-fault negative tests. */
export interface NegativeCoverage {
  totalTuples: number;
  coveredTuples: number;
  omittedTuples: number;
  coverageRatio: number;
}

export interface GenerateResult {
  tests: TestCase[];
  negativeTests: TestCase[];
  /** Present when the input includes invalid values. */
  negativeCoverage?: NegativeCoverage;
  coverage: number;
  /** Uncovered tuples with context. Empty when coverage is 1.0. */
  uncovered: UncoveredTuple[];
  /** Total uncovered tuple count before diagnostic truncation. */
  uncoveredCount: number;
  /** Number of uncovered tuples omitted from `uncovered`. */
  omittedUncovered: number;
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
  uncoveredCount: number;
  omittedUncovered: number;
  /** Rows excluded from coverage accounting and the reason for each. */
  invalidTests: Array<{ testIndex: number; reason: string }>;
}

export interface ExtendInput extends GenerateInput {
  /**
   * How to handle existing tests. `'strict'` keeps every existing test exactly
   * as-is and only appends new tests to improve coverage. This is currently the
   * only supported mode and the default; any other value is rejected with a
   * {@link CoverwiseError}.
   */
  mode?: 'strict';
}

export interface ModelStats {
  parameterCount: number;
  totalValues: number;
  strength: number;
  /** Raw global + sub-model tuple upper bound before constraint exclusion. */
  totalTuples: number;
  /**
   * Coarse sizing heuristic from the largest value count, the strength and the
   * parameter count, capped at `totalTuples`. Not a bound in either direction:
   * a generated suite may be smaller or larger.
   */
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
