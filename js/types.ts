/// Public type definitions for the coverwise API.

export interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;
  aliases?: string[];
}

export interface Parameter {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
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
  /** Why this tuple is uncovered. */
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
 * Structured error with context.
 * Errors are always explanatory — never just a code.
 */
export interface CoverwiseError {
  code: 'CONSTRAINT_ERROR' | 'INSUFFICIENT_COVERAGE' | 'INVALID_INPUT' | 'TUPLE_EXPLOSION';
  message: string;
  detail?: string;
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
