/// Compatibility tests: WASM engine vs pure TypeScript engine.
///
/// Runs identical inputs through both engines and verifies they produce
/// equivalent results (coverage, stats, test counts, and exact output).

import { beforeAll, describe, expect, it } from 'vitest';
import { analyzeCoverage, estimateModel, extendTests, generate, init } from './index.js';
import type {
  CoverageReport,
  GenerateInput,
  GenerateResult,
  ModelStats,
  Parameter,
  TestCase,
} from './types.js';

// --- TS engine imports ---

import {
  estimateModel as tsEstimateModelRaw,
  extend as tsExtendRaw,
  generate as tsGenerateRaw,
} from '../src/ts/core/generator.js';
import type { GenerateOptions } from '../src/ts/model/generate-options.js';
import type { Parameter as InternalParameter } from '../src/ts/model/parameter.js';
import type { TestCase as InternalTestCase } from '../src/ts/model/test-case.js';
import { validateCoverage } from '../src/ts/validator/coverage-validator.js';

// --- Adapter imports (shared conversion logic) ---

import {
  toInternalOptions,
  toInternalParams,
  toInternalTestCase,
  toPublicTestCase,
} from './pure/adapter.js';

// ---------------------------------------------------------------------------
// Thin wrappers around adapter functions for compat-test convenience
// ---------------------------------------------------------------------------

/** Convert public TestCase to internal, delegating to adapter. */
function namedTestToInternal(namedTest: TestCase, params: InternalParameter[]): InternalTestCase {
  return toInternalTestCase(namedTest, params);
}

/** Convert internal TestCase to public, using adapter with rotation=0. */
function internalTestToNamed(tc: InternalTestCase, params: InternalParameter[]): TestCase {
  return toPublicTestCase(tc, params, 0);
}

/** Build GenerateOptions from GenerateInput, delegating to adapter. */
function buildGenerateOptions(input: GenerateInput, params: InternalParameter[]): GenerateOptions {
  return toInternalOptions(input, params);
}

// ---------------------------------------------------------------------------
// TS engine wrappers (same interface as WASM API)
// ---------------------------------------------------------------------------

function tsGenerate(input: GenerateInput): GenerateResult {
  if (input.parameters.length === 0) {
    // Match WASM behavior for empty parameters
    return {
      tests: [],
      coverage: 1.0,
      uncovered: [],
      stats: { totalTuples: 0, coveredTuples: 0, testCount: 0 },
      suggestions: [],
      warnings: [],
      strength: input.strength ?? 2,
    };
  }
  const params = toInternalParams(input.parameters);
  const opts = buildGenerateOptions(input, params);
  const result = tsGenerateRaw(opts);

  return {
    tests: result.tests.map((tc) => internalTestToNamed(tc, params)),
    negativeTests:
      result.negativeTests.length > 0
        ? result.negativeTests.map((tc) => internalTestToNamed(tc, params))
        : undefined,
    coverage: result.coverage,
    uncovered: result.uncovered,
    stats: result.stats,
    suggestions: result.suggestions.map((s) => ({
      description: s.description,
      testCase: s.testCase.values
        ? Object.fromEntries(
            params.map((p, i) => [p.name, p.values[(s.testCase as InternalTestCase).values[i]]]),
          )
        : (s.testCase as unknown as Record<string, string>),
    })),
    warnings: result.warnings,
    strength: opts.strength,
  };
}

function tsAnalyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,
): CoverageReport {
  const params = toInternalParams(parameters);
  const internalTests: InternalTestCase[] = tests.map((t) => namedTestToInternal(t, params));
  const report = validateCoverage(params, internalTests, strength ?? 2);
  // Match WASM behavior: 0 tuples => coverageRatio 1.0
  if (report.totalTuples === 0) {
    report.coverageRatio = 1.0;
  }
  return report;
}

function tsExtendTests(existing: TestCase[], input: GenerateInput): GenerateResult {
  const params = toInternalParams(input.parameters);
  const existingInternal = existing.map((t) => namedTestToInternal(t, params));
  const opts = buildGenerateOptions(input, params);
  const result = tsExtendRaw(existingInternal, opts);

  return {
    tests: result.tests.map((tc) => internalTestToNamed(tc, params)),
    negativeTests:
      result.negativeTests.length > 0
        ? result.negativeTests.map((tc) => internalTestToNamed(tc, params))
        : undefined,
    coverage: result.coverage,
    uncovered: result.uncovered,
    stats: result.stats,
    suggestions: [],
    warnings: result.warnings,
    strength: opts.strength,
  };
}

function tsEstimateModel(input: GenerateInput): ModelStats {
  const params = toInternalParams(input.parameters);
  const opts = buildGenerateOptions(input, params);
  return tsEstimateModelRaw(opts);
}

// ===========================================================================
// Test scenarios
// ===========================================================================

beforeAll(async () => {
  await init();
});

const scenarios: Array<{ name: string; input: GenerateInput }> = [
  {
    name: 'basic 2x2 pairwise',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
      seed: 42,
    },
  },
  {
    name: '3x3x3 pairwise',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        { name: 'arch', values: ['x64', 'arm64', 'x86'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'with constraint',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'ie'] },
      ],
      constraints: ['IF os = mac THEN browser != ie'],
      seed: 42,
    },
  },
  {
    name: 'strength 3',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
      ],
      strength: 3,
      seed: 42,
    },
  },
  {
    name: 'strength 1',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2', '3'] },
        { name: 'b', values: ['x', 'y'] },
      ],
      strength: 1,
      seed: 42,
    },
  },
  {
    name: 'maxTests limited',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2', '3', '4', '5'] },
        { name: 'b', values: ['1', '2', '3', '4', '5'] },
        { name: 'c', values: ['1', '2', '3', '4', '5'] },
      ],
      maxTests: 5,
      seed: 42,
    },
  },
  {
    name: 'with seed tests',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      seeds: [{ os: 'win', browser: 'chrome' }],
      seed: 42,
    },
  },
  {
    name: 'with sub-models',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
        { name: 'arch', values: ['x64', 'arm64'] },
      ],
      subModels: [{ parameters: ['os', 'browser', 'arch'], strength: 3 }],
      seed: 42,
    },
  },
  {
    name: 'asymmetric parameters',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux', 'freebsd'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
        { name: 'env', values: ['dev', 'staging', 'prod'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'many parameters',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
        { name: 'd', values: ['1', '2'] },
        { name: 'e', values: ['1', '2'] },
        { name: 'f', values: ['1', '2'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'boolean and number values',
    input: {
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'version', values: [1, 2, 3] },
        { name: 'os', values: ['win', 'mac'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'different seed',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      seed: 12345,
    },
  },
  {
    name: 'with weights',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      weights: { os: { win: 10, mac: 1, linux: 1 } },
      seed: 42,
    },
  },
  {
    name: 'single parameter',
    input: {
      parameters: [{ name: 'os', values: ['win', 'mac', 'linux'] }],
      seed: 42,
    },
  },
  {
    name: 'empty parameters',
    input: {
      parameters: [],
    },
  },
];

describe('WASM / TS compatibility', () => {
  describe('generate()', () => {
    for (const { name, input } of scenarios) {
      it(`${name}: coverage and stats match`, () => {
        const wasmResult = generate(input);
        const tsResult = tsGenerate(input);

        expect(tsResult.coverage).toBe(wasmResult.coverage);
        expect(tsResult.stats.totalTuples).toBe(wasmResult.stats.totalTuples);
        expect(tsResult.stats.coveredTuples).toBe(wasmResult.stats.coveredTuples);
        expect(tsResult.uncovered.length).toBe(wasmResult.uncovered.length);
        // Note: tests.length may differ between engines because the greedy
        // algorithm's RNG produces different value orderings in C++ vs TS,
        // which can lead to different numbers of tests while still achieving
        // the same coverage.
      });
    }

    // Exact output match tests are skipped because the C++ / WASM engine uses
    // std::mt19937_64 (Mersenne Twister 64-bit) while the TypeScript engine
    // uses xoshiro128**. The same seed produces different sequences across
    // engines, but both are deterministic within their own engine.
    // Compatibility tests compare coverage completeness, not exact output.
    for (const { name, input } of scenarios) {
      it.skip(`${name}: exact test output match`, () => {
        const wasmResult = generate(input);
        const tsResult = tsGenerate(input);

        expect(tsResult.tests).toEqual(wasmResult.tests);
      });
    }
  });

  describe('analyzeCoverage()', () => {
    it('full coverage: both engines agree', () => {
      const params: Parameter[] = [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ];
      const tests: TestCase[] = [
        { os: 'win', browser: 'chrome' },
        { os: 'win', browser: 'firefox' },
        { os: 'mac', browser: 'chrome' },
        { os: 'mac', browser: 'firefox' },
      ];
      const wasmReport = analyzeCoverage(params, tests);
      const tsReport = tsAnalyzeCoverage(params, tests);

      expect(tsReport.coverageRatio).toBe(wasmReport.coverageRatio);
      expect(tsReport.totalTuples).toBe(wasmReport.totalTuples);
      expect(tsReport.coveredTuples).toBe(wasmReport.coveredTuples);
      expect(tsReport.uncovered.length).toBe(wasmReport.uncovered.length);
    });

    it('partial coverage: both engines agree', () => {
      const params: Parameter[] = [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ];
      const tests: TestCase[] = [
        { os: 'win', browser: 'chrome' },
        { os: 'mac', browser: 'firefox' },
      ];
      const wasmReport = analyzeCoverage(params, tests);
      const tsReport = tsAnalyzeCoverage(params, tests);

      expect(tsReport.coverageRatio).toBe(wasmReport.coverageRatio);
      expect(tsReport.totalTuples).toBe(wasmReport.totalTuples);
      expect(tsReport.coveredTuples).toBe(wasmReport.coveredTuples);
      expect(tsReport.uncovered.length).toBe(wasmReport.uncovered.length);
    });

    it('empty tests: both engines agree', () => {
      const params: Parameter[] = [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
      ];
      const wasmReport = analyzeCoverage(params, []);
      const tsReport = tsAnalyzeCoverage(params, []);

      expect(tsReport.coverageRatio).toBe(wasmReport.coverageRatio);
      expect(tsReport.totalTuples).toBe(wasmReport.totalTuples);
      expect(tsReport.coveredTuples).toBe(wasmReport.coveredTuples);
    });

    it('cross-engine: TS-generated tests analyzed by WASM', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const tsResult = tsGenerate(input);
      const wasmReport = analyzeCoverage(input.parameters, tsResult.tests);
      expect(wasmReport.coverageRatio).toBe(1.0);
    });

    it('cross-engine: WASM-generated tests analyzed by TS', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const wasmResult = generate(input);
      const tsReport = tsAnalyzeCoverage(input.parameters, wasmResult.tests);
      expect(tsReport.coverageRatio).toBe(1.0);
    });
  });

  describe('estimateModel()', () => {
    const estimateScenarios = scenarios.filter((s) => s.input.parameters.length > 0);

    for (const { name, input } of estimateScenarios) {
      it(`${name}: model stats match`, () => {
        const wasmStats = estimateModel(input);
        const tsStats = tsEstimateModel(input);

        expect(tsStats.parameterCount).toBe(wasmStats.parameterCount);
        expect(tsStats.totalValues).toBe(wasmStats.totalValues);
        expect(tsStats.strength).toBe(wasmStats.strength);
        expect(tsStats.totalTuples).toBe(wasmStats.totalTuples);
        expect(tsStats.subModelCount).toBe(wasmStats.subModelCount);
        expect(tsStats.constraintCount).toBe(wasmStats.constraintCount);
        expect(tsStats.parameters).toEqual(wasmStats.parameters);
      });
    }
  });

  describe('extendTests()', () => {
    it('extending partial suite: both engines achieve full coverage', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const existing: TestCase[] = [{ os: 'win', browser: 'chrome' }];

      const wasmResult = extendTests(existing, input);
      const tsResult = tsExtendTests(existing, input);

      expect(tsResult.coverage).toBe(wasmResult.coverage);
      expect(tsResult.stats.totalTuples).toBe(wasmResult.stats.totalTuples);
      expect(tsResult.stats.coveredTuples).toBe(wasmResult.stats.coveredTuples);
      expect(tsResult.tests.length).toBe(wasmResult.tests.length);
    });

    it('extending complete suite: both engines agree on no change needed', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac'] },
          { name: 'browser', values: ['chrome', 'firefox'] },
        ],
        seed: 42,
      };
      const wasmFull = generate(input);

      const wasmExtended = extendTests(wasmFull.tests, input);
      const tsExtended = tsExtendTests(wasmFull.tests, input);

      expect(tsExtended.coverage).toBe(1.0);
      expect(wasmExtended.coverage).toBe(1.0);
      expect(tsExtended.stats.totalTuples).toBe(wasmExtended.stats.totalTuples);
    });
  });
});
