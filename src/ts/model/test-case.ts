/// Test case and result representations.

/** Sentinel value indicating an unassigned parameter (matches C++ UINT32_MAX). */
export const UNASSIGNED = 0xffffffff;

/** A single test case: an array of value indices, one per parameter. */
export interface TestCase {
  /** values[i] is the index into Parameter[i].values. */
  values: number[];
}

/** A human-readable representation of an uncovered tuple. */
export interface UncoveredTuple {
  /** e.g. ["os=win", "browser=safari"] */
  tuple: string[];
  /** Parameter names involved, e.g. ["os", "browser"] */
  params: string[];
  /** Why this tuple is uncovered. */
  reason: string;
}

/** Generation statistics for evaluation and comparison. */
export interface GenerateStats {
  totalTuples: number;
  coveredTuples: number;
  testCount: number;
}

/** Equivalence class coverage metrics. */
export interface ClassCoverage {
  totalClassTuples: number;
  coveredClassTuples: number;
  classCoverageRatio: number;
}

/** Suggested test case to add (for AI/human guidance). */
export interface Suggestion {
  /** e.g. "Add test: os=win, browser=safari" */
  description: string;
  testCase: TestCase;
}

/**
 * Result of test generation.
 *
 * Note on sub-model metrics: When sub-models are used, `coverage` reports
 * the minimum coverage ratio across all engines (global + sub-models),
 * while `stats.totalTuples` and `stats.coveredTuples` are the sum across
 * all engines. Thus `stats.coveredTuples / stats.totalTuples` may differ
 * from `coverage`. Use `coverage` for pass/fail decisions; use `stats` for
 * understanding total workload.
 */
export interface GenerateResult {
  /** Positive tests (no invalid values). */
  tests: TestCase[];
  /** Negative tests (exactly 1 invalid value each). */
  negativeTests: TestCase[];
  /** Minimum coverage ratio across all engines. */
  coverage: number;
  uncovered: UncoveredTuple[];
  stats: GenerateStats;
  suggestions: Suggestion[];
  warnings: string[];
  /** Equivalence class coverage (if classes defined). */
  classCoverage?: ClassCoverage;
}

/** Create a default UncoveredTuple. */
export function createUncoveredTuple(
  tuple: string[] = [],
  params: string[] = [],
  reason = 'never covered',
): UncoveredTuple {
  return { tuple, params, reason };
}

/** Format an UncoveredTuple as a readable string: "os=win, browser=safari". */
export function uncoveredTupleToString(ut: UncoveredTuple): string {
  return ut.tuple.join(', ');
}

/** Create a default GenerateStats. */
export function createGenerateStats(): GenerateStats {
  return { totalTuples: 0, coveredTuples: 0, testCount: 0 };
}

/** Create a default GenerateResult. */
export function createGenerateResult(): GenerateResult {
  return {
    tests: [],
    negativeTests: [],
    coverage: 0,
    uncovered: [],
    stats: createGenerateStats(),
    suggestions: [],
    warnings: [],
  };
}
