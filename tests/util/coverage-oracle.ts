/// @file coverage-oracle.ts
/// @brief The one place a generated suite's coverage is recounted for the
///        TypeScript test tiers.
///
/// A generator's own `coverage` and `uncovered` are what is under test, so a
/// test cannot read its verdict from them. The recount here goes through
/// `analyzeCoverage`, the validator path, which enumerates tuples itself and
/// never touches generator internals.

import { expect } from 'vitest';
import { analyzeCoverage } from '../../js/index.js';
import type { GenerateInput, TestCase, UncoveredTuple } from '../../js/types.js';

/// Anything with the `analyzeCoverage` signature, so the same recount serves
/// the WASM and the pure surface.
export type AnalyzeCoverage = typeof analyzeCoverage;

/// The slice of a generate/extend result the recount is compared against.
export interface ReportedSuite {
  tests: TestCase[];
  coverage: number;
  stats: { totalTuples: number; coveredTuples: number };
  uncovered: Array<Pick<UncoveredTuple, 'tuple'>>;
  uncoveredCount?: number;
}

/// Coverage of a suite as the validator counts it.
///
/// With sub-models the counts are summed over the global layer and each
/// sub-model layer, which are recounted without constraints; a tuple shared by
/// two layers is counted in both, so only `ratio` (the minimum across layers)
/// and `uncoveredCount` are comparable there.
export interface Recount {
  total: number;
  covered: number;
  ratio: number;
  uncoveredCount: number;
  uncovered: UncoveredTuple[];
}

/// Recount `tests` against the model in `input`, independent of the generator.
export function recountCoverage(
  input: Pick<GenerateInput, 'parameters' | 'constraints' | 'strength' | 'subModels'>,
  tests: TestCase[],
  analyze: AnalyzeCoverage = analyzeCoverage,
): Recount {
  const layers = [
    analyze(input.parameters, tests, input.strength ?? 2, input.constraints),
    ...(input.subModels ?? []).map((subModel) =>
      analyze(
        input.parameters.filter((p) => subModel.parameters.includes(p.name)),
        tests,
        subModel.strength,
      ),
    ),
  ];
  const total = layers.reduce((sum, r) => sum + r.totalTuples, 0);
  const covered = layers.reduce((sum, r) => sum + r.coveredTuples, 0);
  // A tuple uncovered in two layers (a sub-model at the global strength) is one tuple.
  const seen = new Set<string>();
  const distinct = layers
    .flatMap((r) => r.uncovered)
    .filter((u) => {
      const key = JSON.stringify(u.tuple);
      if (seen.has(key)) {
        return false;
      }
      seen.add(key);
      return true;
    });
  for (const r of layers) {
    expect(r.uncovered.length, 'recount needs every uncovered tuple listed').toBe(r.uncoveredCount);
  }
  return {
    total,
    covered,
    // With sub-models the documented pass/fail ratio is the minimum across layers.
    ratio: Math.min(...layers.map((r) => r.coverageRatio)),
    uncoveredCount: distinct.length,
    uncovered: distinct,
  };
}

/// Assert a generate/extend result reports exactly what a recount of its rows
/// finds, and return the recount for further assertions.
export function expectReportedCoverageMatchesRecount(
  input: Pick<GenerateInput, 'parameters' | 'constraints' | 'strength' | 'subModels'>,
  result: ReportedSuite,
  analyze: AnalyzeCoverage = analyzeCoverage,
): Recount {
  const recount = recountCoverage(input, result.tests, analyze);
  expect(result.coverage).toBe(recount.ratio);
  // A result without a count lists every uncovered tuple.
  expect(result.uncoveredCount ?? result.uncovered.length).toBe(recount.uncoveredCount);
  if (!input.subModels?.length) {
    expect(result.stats.totalTuples).toBe(recount.total);
    expect(result.stats.coveredTuples).toBe(recount.covered);
  }
  return recount;
}

/// Assert the rows of a result cover every feasible tuple, by recount alone.
export function expectComplete(
  input: Pick<GenerateInput, 'parameters' | 'constraints' | 'strength' | 'subModels'>,
  result: ReportedSuite,
  analyze: AnalyzeCoverage = analyzeCoverage,
): Recount {
  const recount = expectReportedCoverageMatchesRecount(input, result, analyze);
  expect(recount.uncoveredCount).toBe(0);
  expect(recount.ratio).toBe(1);
  return recount;
}

type OracleModel = Pick<GenerateInput, 'parameters' | 'constraints' | 'strength' | 'subModels'>;
type ParameterValueInput = GenerateInput['parameters'][number]['values'][number];

/// Index of the value a rendered row cell names, and whether it is invalid.
function valueIndexOf(parameter: GenerateInput['parameters'][number], cell: unknown) {
  const text = String(cell);
  return parameter.values.findIndex((value) =>
    typeof value === 'object'
      ? [String(value.value), ...(value.aliases ?? [])].includes(text)
      : String(value) === text,
  );
}

function isInvalidValue(value: ParameterValueInput): boolean {
  return typeof value === 'object' && value.invalid === true;
}

/// Where the single invalid value of `row` sits, as `[parameter, value]`, or
/// `undefined` when the row holds none or more than one.
function singleFault(model: OracleModel, row: TestCase): [number, number] | undefined {
  const faults: Array<[number, number]> = [];
  model.parameters.forEach((parameter, pi) => {
    const vi = valueIndexOf(parameter, row[parameter.name]);
    if (vi >= 0 && isInvalidValue(parameter.values[vi])) {
      faults.push([pi, vi]);
    }
  });
  return faults.length === 1 ? faults[0] : undefined;
}

function combinations(n: number, k: number, from = 0): number[][] {
  if (k === 0) {
    return [[]];
  }
  const out: number[][] = [];
  for (let i = from; i <= n - k; i++) {
    for (const rest of combinations(n, k - 1, i + 1)) {
      out.push([i, ...rest]);
    }
  }
  return out;
}

/// Single-fault tuples of an unconstrained model and how many the rows cover.
///
/// A tuple pins one invalid value and fills the rest of its parameters with
/// valid ones. A row counts toward a tuple only if it holds exactly that one
/// invalid value; a preserved single-fault row in `tests` counts, as it does in
/// the engine.
export function recountNegativeCoverage(
  input: OracleModel,
  rows: { tests: TestCase[]; negativeTests?: TestCase[] },
): { total: number; covered: number } {
  expect(input.constraints ?? [], 'negative recount needs an unconstrained model').toEqual([]);
  const strength = input.strength ?? 2;
  const n = input.parameters.length;
  const combos = combinations(n, strength);
  const validCounts = input.parameters.map(
    (p) => p.values.filter((v) => !isInvalidValue(v)).length,
  );

  let total = 0;
  input.parameters.forEach((parameter, fixed) => {
    const invalidCount = parameter.values.filter(isInvalidValue).length;
    for (const combo of combos.filter((c) => c.includes(fixed))) {
      total +=
        invalidCount * combo.filter((pi) => pi !== fixed).reduce((p, pi) => p * validCounts[pi], 1);
    }
  });

  const covered = new Set<string>();
  for (const row of [...(rows.negativeTests ?? []), ...rows.tests]) {
    const fault = singleFault(input, row);
    if (!fault) {
      continue;
    }
    const indices = input.parameters.map((p) => valueIndexOf(p, row[p.name]));
    for (const combo of combos.filter((c) => c.includes(fault[0]))) {
      covered.add(`${combo.join(',')}|${combo.map((pi) => indices[pi]).join(',')}`);
    }
  }
  return { total, covered: covered.size };
}

/// Assert every negative row holds exactly one invalid value and satisfies
/// every constraint, and (for an unconstrained model) that the suite covers
/// every single-fault tuple and agrees with any `negativeCoverage` it reports.
export function expectNegativeSuiteSound(
  input: OracleModel,
  result: {
    tests: TestCase[];
    negativeTests?: TestCase[];
    negativeCoverage?: { totalTuples: number; coveredTuples: number };
  },
  analyze: AnalyzeCoverage = analyzeCoverage,
): void {
  const negatives = result.negativeTests ?? [];
  negatives.forEach((row, index) => {
    expect(
      singleFault(input, row),
      `negative row ${index} must hold exactly one invalid value`,
    ).toBeDefined();
  });

  // With the invalid flags stripped the validator rejects a row only for a constraint.
  if (negatives.length > 0 && input.constraints?.length) {
    const plain = input.parameters.map((parameter) => ({
      ...parameter,
      values: parameter.values.map((value) => {
        if (typeof value !== 'object') {
          return value;
        }
        const { invalid: _invalid, ...rest } = value;
        return rest;
      }),
    })) as GenerateInput['parameters'];
    const report = analyze(plain, negatives, input.strength ?? 2, input.constraints);
    expect(report.invalidTests, 'negative rows must satisfy every constraint').toEqual([]);
  }

  if (!input.constraints?.length) {
    const recount = recountNegativeCoverage(input, result);
    expect(recount.covered).toBe(recount.total);
    if (result.negativeCoverage) {
      expect(result.negativeCoverage.totalTuples).toBe(recount.total);
      expect(result.negativeCoverage.coveredTuples).toBe(recount.covered);
    }
  }
}
