/**
 * coverwise — Combinatorial test design API.
 *
 * Not just a generation tool — a test design engine.
 * JSON-complete, stateless, decomposable.
 */

// --- Public Types ---

export interface Parameter {
  name: string;
  values: (string | number | boolean)[];
}

/**
 * Human-readable constraint syntax.
 * Examples:
 *   "IF os=mac THEN browser!=ie"
 *   "NOT (os=win AND browser=safari)"
 *   "os=linux IMPLIES arch!=arm"
 */
export interface Constraint {
  expression: string;
}

/**
 * A test case as a readable key-value map.
 * e.g. { os: "win", browser: "chrome", arch: "x64" }
 */
export interface TestCase {
  [parameterName: string]: string | number | boolean;
}

export interface WeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}

export interface GenerateInput {
  parameters: Parameter[];
  constraints?: Constraint[];
  strength?: number; // default: 2 (pairwise)
  seed?: number;
  weights?: WeightConfig;
  seeds?: TestCase[]; // existing tests to build upon
  maxTests?: number; // 0 = no limit
}

/**
 * A human-readable uncovered tuple with context.
 */
export interface UncoveredTuple {
  /** e.g. ["os=win", "browser=safari"] */
  tuple: string[];
  /** Parameter names involved. */
  params: string[];
  /** Why this tuple is uncovered. */
  reason: string;
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
  coverage: number; // 0.0 - 1.0
  /** Uncovered tuples with context. Empty when coverage is 1.0. */
  uncovered: UncoveredTuple[];
  /** Statistics for evaluation and comparison. */
  stats: GenerateStats;
  /** Actionable suggestions. e.g. "Add test: os=win, browser=safari" */
  suggestions: string[];
  /** Warnings (e.g. performance degradation). */
  warnings: string[];
  /** Strength used for generation. */
  strength: number;
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
  mode?: 'strict' | 'optimize';
}

/**
 * Structured error with context.
 * Errors are always explanatory — never just a code.
 */
export interface CoverwiseError {
  code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  message: string;
  detail?: string;
}

// --- Core API ---

/**
 * Generate a covering array. One function, sensible defaults.
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
 * // result.stats: { totalTuples: 18, coveredTuples: 18, testCount: 9 }
 * // result.uncovered: []
 * // result.suggestions: []
 */
export function generate(_input: GenerateInput): GenerateResult {
  // TODO: Call WASM core via embind
  throw new Error('Not yet implemented — WASM module not loaded');
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
 * // report.uncovered: [{ tuple: ["os=mac", "browser=chrome"], params: ["os", "browser"], reason: "never covered" }]
 */
export function analyzeCoverage(
  _parameters: Parameter[],
  _tests: TestCase[],
  _strength?: number,
): CoverageReport {
  // TODO: Call WASM core via embind
  throw new Error('Not yet implemented — WASM module not loaded');
}

/**
 * Extend an existing test suite with additional tests to improve coverage.
 *
 * mode: "strict" (default) keeps existing tests as-is.
 * mode: "optimize" allows reorganizing for better overall coverage.
 */
export function extendTests(_existing: TestCase[], _input: ExtendInput): GenerateResult {
  // TODO: Call WASM core via embind
  throw new Error('Not yet implemented — WASM module not loaded');
}
