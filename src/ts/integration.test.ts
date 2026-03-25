/// Integration tests for the pure TypeScript engine, mirroring js/index.test.ts.
///
/// The WASM API uses named key-value maps ({ os: 'win', browser: 'chrome' }),
/// while the TS engine uses index-based TestCase ({ values: [0, 0] }).
/// The adapter functions below bridge these representations.

import { describe, expect, it } from 'vitest';
import { estimateModel, extend, generate } from './core/generator.js';
import {
  createGenerateOptions,
  createWeightConfig,
  type GenerateOptions,
  type SubModel as InternalSubModel,
  type WeightConfig as InternalWeightConfig,
} from './model/generate-options.js';
import { Parameter } from './model/parameter.js';
import type { TestCase as InternalTestCase } from './model/test-case.js';
import { type CoverageReport, validateCoverage } from './validator/coverage-validator.js';

// ---------------------------------------------------------------------------
// Types matching the WASM public API (from js/types.ts)
// ---------------------------------------------------------------------------

interface ParameterValue {
  value: string | number | boolean;
  invalid?: boolean;
  aliases?: string[];
}

interface WasmParameter {
  name: string;
  values: (string | number | boolean | ParameterValue)[];
}

interface WasmSubModel {
  parameters: string[];
  strength: number;
}

interface WasmWeightConfig {
  [parameterName: string]: {
    [value: string]: number;
  };
}

interface WasmGenerateInput {
  parameters: WasmParameter[];
  constraints?: string[];
  strength?: number;
  seed?: number;
  weights?: WasmWeightConfig;
  seeds?: WasmTestCase[];
  maxTests?: number;
  subModels?: WasmSubModel[];
}

interface WasmTestCase {
  [parameterName: string]: string | number | boolean;
}

interface WasmUncoveredTuple {
  tuple: string[];
  params: string[];
  reason: string;
}

interface WasmGenerateStats {
  totalTuples: number;
  coveredTuples: number;
  testCount: number;
}

interface WasmGenerateResult {
  tests: WasmTestCase[];
  negativeTests?: WasmTestCase[];
  coverage: number;
  uncovered: WasmUncoveredTuple[];
  stats: WasmGenerateStats;
}

// ---------------------------------------------------------------------------
// Adapter: convert WASM-style parameters to internal Parameter[]
// ---------------------------------------------------------------------------

function toStringValue(v: string | number | boolean): string {
  return String(v);
}

function convertParameters(wasmParams: WasmParameter[]): Parameter[] {
  return wasmParams.map((wp) => {
    const values: string[] = [];
    const invalid: boolean[] = [];
    const aliases: string[][] = [];
    let hasInvalid = false;
    let hasAliases = false;

    for (const v of wp.values) {
      if (typeof v === 'object' && v !== null && 'value' in v) {
        const pv = v as ParameterValue;
        values.push(toStringValue(pv.value));
        invalid.push(pv.invalid === true);
        aliases.push(pv.aliases ?? []);
        if (pv.invalid) {
          hasInvalid = true;
        }
        if (pv.aliases && pv.aliases.length > 0) {
          hasAliases = true;
        }
      } else {
        values.push(toStringValue(v));
        invalid.push(false);
        aliases.push([]);
      }
    }

    const param = hasInvalid
      ? new Parameter(wp.name, values, invalid)
      : new Parameter(wp.name, values);
    if (hasAliases) {
      param.setAliases(aliases);
    }
    return param;
  });
}

// ---------------------------------------------------------------------------
// Adapter: convert named seed tests to index-based TestCase
// ---------------------------------------------------------------------------

function namedTestToInternal(namedTest: WasmTestCase, params: Parameter[]): InternalTestCase {
  const values: number[] = new Array(params.length);
  for (let i = 0; i < params.length; ++i) {
    const rawVal = namedTest[params[i].name];
    const strVal = toStringValue(rawVal);
    const idx = params[i].findValueIndex(strVal);
    values[i] = idx;
  }
  return { values };
}

// ---------------------------------------------------------------------------
// Adapter: convert index-based TestCase to named map
// ---------------------------------------------------------------------------

function internalTestToNamed(tc: InternalTestCase, params: Parameter[]): WasmTestCase {
  const result: WasmTestCase = {};
  for (let i = 0; i < params.length; ++i) {
    result[params[i].name] = params[i].values[tc.values[i]];
  }
  return result;
}

// ---------------------------------------------------------------------------
// Adapter: build GenerateOptions from WASM-style input
// ---------------------------------------------------------------------------

function buildGenerateOptions(input: WasmGenerateInput, params: Parameter[]): GenerateOptions {
  // Convert sub-models: { parameters: [...] } -> { parameterNames: [...] }
  const subModels: InternalSubModel[] = (input.subModels ?? []).map((sm) => ({
    parameterNames: sm.parameters,
    strength: sm.strength,
  }));

  // Convert weights: flat { os: { win: 100 } } -> { entries: { os: { win: 100 } } }
  let weights: InternalWeightConfig = createWeightConfig();
  if (input.weights) {
    weights = { entries: { ...input.weights } };
  }

  // Convert seed tests
  const seeds: InternalTestCase[] = (input.seeds ?? []).map((s) => namedTestToInternal(s, params));

  return createGenerateOptions({
    parameters: params.map((p) => ({
      name: p.name,
      values: p.values,
      ...(p.hasInvalidValues ? { invalid: p.invalid } : {}),
    })),
    constraintExpressions: input.constraints ?? [],
    strength: input.strength ?? 2,
    seed: input.seed ?? 0,
    maxTests: input.maxTests ?? 0,
    seeds,
    subModels,
    weights,
  });
}

// ---------------------------------------------------------------------------
// tsGenerate
// ---------------------------------------------------------------------------

function tsGenerate(input: WasmGenerateInput): WasmGenerateResult {
  const params = convertParameters(input.parameters);
  const opts = buildGenerateOptions(input, params);
  const result = generate(opts);

  return {
    tests: result.tests.map((tc) => internalTestToNamed(tc, params)),
    negativeTests:
      result.negativeTests.length > 0
        ? result.negativeTests.map((tc) => internalTestToNamed(tc, params))
        : undefined,
    coverage: result.coverage,
    uncovered: result.uncovered,
    stats: result.stats,
  };
}

// ---------------------------------------------------------------------------
// tsAnalyzeCoverage
// ---------------------------------------------------------------------------

function tsAnalyzeCoverage(
  parameters: WasmParameter[],
  tests: WasmTestCase[],
  strength?: number,
): CoverageReport {
  const params = convertParameters(parameters);
  const internalTests: InternalTestCase[] = tests.map((t) => namedTestToInternal(t, params));
  const report = validateCoverage(params, internalTests, strength ?? 2);
  return report;
}

// ---------------------------------------------------------------------------
// tsExtendTests
// ---------------------------------------------------------------------------

function tsExtendTests(existing: WasmTestCase[], input: WasmGenerateInput): WasmGenerateResult {
  const params = convertParameters(input.parameters);
  const existingInternal = existing.map((t) => namedTestToInternal(t, params));
  const opts = buildGenerateOptions(input, params);
  const result = extend(existingInternal, opts);

  return {
    tests: result.tests.map((tc) => internalTestToNamed(tc, params)),
    negativeTests:
      result.negativeTests.length > 0
        ? result.negativeTests.map((tc) => internalTestToNamed(tc, params))
        : undefined,
    coverage: result.coverage,
    uncovered: result.uncovered,
    stats: result.stats,
  };
}

// ---------------------------------------------------------------------------
// tsEstimateModel
// ---------------------------------------------------------------------------

function tsEstimateModel(input: WasmGenerateInput) {
  const params = convertParameters(input.parameters);
  const opts = buildGenerateOptions(input, params);
  return estimateModel(opts);
}

// ===========================================================================
// Tests — mirroring js/index.test.ts
// ===========================================================================

describe('generate()', () => {
  it('basic pairwise: 2 params x 2 values -> full coverage', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(0);
    expect(result.uncovered).toHaveLength(0);
  });

  it('3 params x 3 values -> full coverage', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        { name: 'arch', values: ['x64', 'arm64', 'x86'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.stats.totalTuples).toBe(27);
  });

  it('with constraints: IF os=mac THEN browser!=ie', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'ie'] },
      ],
      constraints: ['IF os = mac THEN browser != ie'],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      if (test.os === 'mac') {
        expect(test.browser).not.toBe('ie');
      }
    }
  });

  it('strength 3 -> full coverage', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
      ],
      strength: 3,
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBe(8);
  });

  it('determinism: same seed -> same tests', () => {
    const input: WasmGenerateInput = {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      seed: 42,
    };
    const r1 = tsGenerate(input);
    const r2 = tsGenerate(input);
    expect(r1.tests).toEqual(r2.tests);
  });

  it('maxTests limits output count', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'a', values: ['1', '2', '3', '4', '5'] },
        { name: 'b', values: ['1', '2', '3', '4', '5'] },
        { name: 'c', values: ['1', '2', '3', '4', '5'] },
        { name: 'd', values: ['1', '2', '3', '4', '5'] },
      ],
      maxTests: 3,
    });
    expect(result.tests.length).toBeLessThanOrEqual(3);
  });

  it('seed tests are included in output', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
      seeds: [{ os: 'win', browser: 'chrome' }],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests[0]).toEqual({ os: 'win', browser: 'chrome' });
  });

  it('with subModels: sub-model coverage achieved', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
        { name: 'arch', values: ['x64', 'arm64'] },
      ],
      strength: 2,
      subModels: [{ parameters: ['os', 'browser', 'arch'], strength: 3 }],
    });
    expect(result.coverage).toBe(1.0);
  });

  it('with invalid values: generates negative tests', () => {
    const result = tsGenerate({
      parameters: [
        {
          name: 'browser',
          values: ['chrome', 'firefox', { value: 'ie6', invalid: true }],
        },
        { name: 'os', values: ['win', 'mac'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.negativeTests).toBeDefined();
    expect(result.negativeTests?.length).toBeGreaterThan(0);
    for (const nt of result.negativeTests ?? []) {
      expect(nt.browser).toBe('ie6');
    }
  });

  it('with weights: higher weight values are preferred', () => {
    const counts: Record<string, number> = { win: 0, mac: 0, linux: 0 };
    for (let seed = 0; seed < 10; seed++) {
      const result = tsGenerate({
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
          { name: 'arch', values: ['x64', 'arm64'] },
        ],
        weights: { os: { win: 100, mac: 1, linux: 1 } },
        seed,
      });
      for (const test of result.tests) {
        counts[test.os as string]++;
      }
    }
    expect(counts.win).toBeGreaterThan(counts.mac);
  });
});

describe('analyzeCoverage()', () => {
  const params: WasmParameter[] = [
    { name: 'os', values: ['win', 'mac'] },
    { name: 'browser', values: ['chrome', 'firefox'] },
  ];

  it('full coverage -> coverageRatio 1.0', () => {
    const tests: WasmTestCase[] = [
      { os: 'win', browser: 'chrome' },
      { os: 'win', browser: 'firefox' },
      { os: 'mac', browser: 'chrome' },
      { os: 'mac', browser: 'firefox' },
    ];
    const report = tsAnalyzeCoverage(params, tests);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);
  });

  it('partial coverage -> uncovered tuples present', () => {
    const tests: WasmTestCase[] = [{ os: 'win', browser: 'chrome' }];
    const report = tsAnalyzeCoverage(params, tests);
    expect(report.coverageRatio).toBeLessThan(1.0);
    expect(report.uncovered.length).toBeGreaterThan(0);
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(1);
  });

  it('default strength is 2', () => {
    const tests: WasmTestCase[] = [
      { os: 'win', browser: 'chrome' },
      { os: 'win', browser: 'firefox' },
      { os: 'mac', browser: 'chrome' },
      { os: 'mac', browser: 'firefox' },
    ];
    const report = tsAnalyzeCoverage(params, tests);
    expect(report.totalTuples).toBe(4);
  });
});

describe('extendTests()', () => {
  const params: WasmParameter[] = [
    { name: 'os', values: ['win', 'mac', 'linux'] },
    { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
  ];

  it('extends partial test suite -> improved coverage', () => {
    const existing: WasmTestCase[] = [{ os: 'win', browser: 'chrome' }];
    const result = tsExtendTests(existing, { parameters: params });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(1);
    expect(result.tests[0]).toEqual({ os: 'win', browser: 'chrome' });
  });

  it('extends already complete suite -> keeps coverage', () => {
    const full = tsGenerate({ parameters: params });
    const result = tsExtendTests(full.tests, { parameters: params });
    expect(result.coverage).toBe(1.0);
  });
});

describe('estimateModel()', () => {
  it('returns correct model statistics', () => {
    const stats = tsEstimateModel({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
    });
    expect(stats.parameterCount).toBe(2);
    expect(stats.totalValues).toBe(5);
    expect(stats.strength).toBe(2);
    expect(stats.totalTuples).toBe(6);
    expect(stats.estimatedTests).toBeGreaterThan(0);
    expect(stats.subModelCount).toBe(0);
    expect(stats.constraintCount).toBe(0);
    expect(stats.parameters).toHaveLength(2);
    expect(stats.parameters[0]).toEqual({ name: 'os', valueCount: 3, invalidCount: 0 });
    expect(stats.parameters[1]).toEqual({ name: 'browser', valueCount: 2, invalidCount: 0 });
  });

  it('reports sub-model and constraint counts', () => {
    const stats = tsEstimateModel({
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
      ],
      constraints: ['IF a = "1" THEN b != "2"'],
      subModels: [{ parameters: ['a', 'b'], strength: 2 }],
    });
    expect(stats.subModelCount).toBe(1);
    expect(stats.constraintCount).toBe(1);
  });
});

describe('edge cases', () => {
  it('empty parameters -> coverage 1.0, no tests', () => {
    const result = tsGenerate({ parameters: [] });
    expect(result.coverage).toBe(1.0);
    expect(result.tests).toHaveLength(0);
    expect(result.stats.totalTuples).toBe(0);
  });

  it('single parameter -> coverage 1.0 (no pairs)', () => {
    const result = tsGenerate({
      parameters: [{ name: 'os', values: ['win', 'mac', 'linux'] }],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.stats.totalTuples).toBe(0);
  });

  it('parameter with single value', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'os', values: ['win'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      expect(test.os).toBe('win');
    }
  });

  it('strength = 1 -> each value appears at least once', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'a', values: ['1', '2', '3'] },
        { name: 'b', values: ['x', 'y'] },
      ],
      strength: 1,
    });
    expect(result.coverage).toBe(1.0);
    expect(result.stats.totalTuples).toBe(5);
  });

  it('strength > param count -> coverage 1.0, no tests', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
      ],
      strength: 5,
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests).toHaveLength(0);
  });

  it('maxTests = 1 -> incomplete coverage', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'a', values: ['1', '2', '3'] },
        { name: 'b', values: ['1', '2', '3'] },
      ],
      maxTests: 1,
    });
    expect(result.tests).toHaveLength(1);
    expect(result.coverage).toBeLessThan(1.0);
    expect(result.uncovered.length).toBeGreaterThan(0);
  });

  it('seed = 0 and omitted seed produce same result', () => {
    const params: WasmParameter[] = [
      { name: 'a', values: ['1', '2'] },
      { name: 'b', values: ['1', '2'] },
    ];
    const r1 = tsGenerate({ parameters: params, seed: 0 });
    const r2 = tsGenerate({ parameters: params });
    expect(r1.tests).toEqual(r2.tests);
  });

  it('state isolation: interleaved seeds produce correct results', () => {
    const params: WasmParameter[] = [
      { name: 'a', values: ['1', '2', '3'] },
      { name: 'b', values: ['1', '2', '3'] },
    ];
    const r1 = tsGenerate({ parameters: params, seed: 42 });
    tsGenerate({ parameters: params, seed: 99 });
    const r3 = tsGenerate({ parameters: params, seed: 42 });
    expect(r3.tests).toEqual(r1.tests);
  });

  it('seeds: [] and omitted seeds produce same result', () => {
    const params: WasmParameter[] = [
      { name: 'a', values: ['1', '2'] },
      { name: 'b', values: ['1', '2'] },
    ];
    const r1 = tsGenerate({ parameters: params, seed: 7, seeds: [] });
    const r2 = tsGenerate({ parameters: params, seed: 7 });
    expect(r1.tests).toEqual(r2.tests);
  });
});

describe('analyzeCoverage() edge cases', () => {
  it('empty tests -> 0 coverage', () => {
    const report = tsAnalyzeCoverage(
      [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
      ],
      [],
    );
    expect(report.coverageRatio).toBe(0.0);
    expect(report.coveredTuples).toBe(0);
    expect(report.totalTuples).toBe(4);
    expect(report.uncovered.length).toBe(4);
  });

  it('single parameter -> 0 tuples for pairwise', () => {
    const report = tsAnalyzeCoverage([{ name: 'a', values: ['1', '2'] }], [{ a: '1' }]);
    expect(report.totalTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
  });
});

describe('extendTests() edge cases', () => {
  it('empty existing -> equivalent to generate', () => {
    const params: WasmParameter[] = [
      { name: 'a', values: ['1', '2'] },
      { name: 'b', values: ['1', '2'] },
    ];
    const result = tsExtendTests([], { parameters: params, seed: 0 });
    const genResult = tsGenerate({ parameters: params, seed: 0 });
    expect(result.coverage).toBe(1.0);
    expect(result.tests).toEqual(genResult.tests);
  });
});

describe('estimateModel() edge cases', () => {
  it('0 parameters', () => {
    const stats = tsEstimateModel({ parameters: [] });
    expect(stats.parameterCount).toBe(0);
    expect(stats.totalValues).toBe(0);
    expect(stats.totalTuples).toBe(0);
  });

  it('1 parameter -> 0 tuples for pairwise', () => {
    const stats = tsEstimateModel({
      parameters: [{ name: 'a', values: ['1', '2', '3'] }],
    });
    expect(stats.parameterCount).toBe(1);
    expect(stats.totalTuples).toBe(0);
  });
});

// ---------------------------------------------------------------------------
// Non-string parameter values (boolean, number)
// ---------------------------------------------------------------------------

describe('boolean parameter values', () => {
  it('values: [true, false] -- generate works', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'os', values: ['win', 'mac'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(0);
    for (const test of result.tests) {
      expect(['true', 'false']).toContain(test.debug);
    }
  });

  it('analyzeCoverage with boolean values', () => {
    const params: WasmParameter[] = [
      { name: 'debug', values: [true, false] },
      { name: 'os', values: ['win', 'mac'] },
    ];
    const tests: WasmTestCase[] = [
      { debug: 'true', os: 'win' },
      { debug: 'false', os: 'mac' },
    ];
    const report = tsAnalyzeCoverage(params, tests);
    expect(report.coverageRatio).toBeLessThan(1.0);
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(2);
  });

  it('analyzeCoverage with boolean test values', () => {
    const params: WasmParameter[] = [
      { name: 'debug', values: [true, false] },
      { name: 'os', values: ['win', 'mac'] },
    ];
    const tests: WasmTestCase[] = [
      { debug: true, os: 'win' },
      { debug: false, os: 'mac' },
      { debug: true, os: 'mac' },
      { debug: false, os: 'win' },
    ];
    const report = tsAnalyzeCoverage(params, tests);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('extendTests with boolean param values', () => {
    const params: WasmParameter[] = [
      { name: 'debug', values: [true, false] },
      { name: 'os', values: ['win', 'mac'] },
    ];
    const existing: WasmTestCase[] = [{ debug: 'true', os: 'win' }];
    const result = tsExtendTests(existing, { parameters: params });
    expect(result.coverage).toBe(1.0);
  });

  it('constraint with boolean param: IF debug = true THEN os != mac', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'os', values: ['win', 'mac', 'linux'] },
      ],
      constraints: ['IF debug = true THEN os != mac'],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      if (test.debug === 'true') {
        expect(test.os).not.toBe('mac');
      }
    }
  });

  it('seed tests with boolean values', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'os', values: ['win', 'mac'] },
      ],
      seeds: [{ debug: 'true', os: 'win' }],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests[0]).toEqual({ debug: 'true', os: 'win' });
  });
});

describe('number parameter values', () => {
  it('values: [1, 2, 3] -- generate works', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'version', values: [1, 2, 3] },
        { name: 'os', values: ['win', 'mac'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(0);
    for (const test of result.tests) {
      expect(['1', '2', '3']).toContain(test.version);
    }
  });

  it('values: [0] -- single numeric value', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'count', values: [0] },
        { name: 'flag', values: ['a', 'b'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      expect(test.count).toBe('0');
    }
  });

  it('negative number values', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'temp', values: [-10, 0, 10] },
        { name: 'unit', values: ['C', 'F'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      expect(['-10', '0', '10']).toContain(test.temp);
    }
  });

  it('float values', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'ratio', values: [0.5, 1.0, 2.0] },
        { name: 'mode', values: ['a', 'b'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
  });

  it('analyzeCoverage with numeric test values', () => {
    const params: WasmParameter[] = [
      { name: 'version', values: [1, 2] },
      { name: 'os', values: ['win', 'mac'] },
    ];
    const tests: WasmTestCase[] = [
      { version: 1, os: 'win' },
      { version: 2, os: 'mac' },
      { version: 1, os: 'mac' },
      { version: 2, os: 'win' },
    ];
    const report = tsAnalyzeCoverage(params, tests);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('constraint with numeric param: IF version > 1 THEN os != linux', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'version', values: [1, 2, 3] },
        { name: 'os', values: ['win', 'mac', 'linux'] },
      ],
      constraints: ['IF version > 1 THEN os != linux'],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      const v = Number(test.version);
      if (v > 1) {
        expect(test.os).not.toBe('linux');
      }
    }
  });
});

describe('mixed type parameter values', () => {
  it('string + number + boolean in same values array', () => {
    const result = tsGenerate({
      parameters: [
        { name: 'setting', values: ['auto', 0, true] },
        { name: 'env', values: ['dev', 'prod'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    for (const test of result.tests) {
      expect(['auto', '0', 'true']).toContain(test.setting);
    }
  });

  it('ParameterValue object with numeric value', () => {
    const result = tsGenerate({
      parameters: [
        {
          name: 'port',
          values: [{ value: 80 }, { value: 443 }, { value: 0, invalid: true }],
        },
        { name: 'proto', values: ['http', 'https'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.negativeTests).toBeDefined();
    expect(result.negativeTests?.length).toBeGreaterThan(0);
    for (const nt of result.negativeTests!) {
      expect(nt.port).toBe('0');
    }
  });

  it('ParameterValue object with boolean value', () => {
    const result = tsGenerate({
      parameters: [
        {
          name: 'flag',
          values: [{ value: true }, { value: false, invalid: true }],
        },
        { name: 'mode', values: ['fast', 'slow'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.negativeTests).toBeDefined();
    for (const nt of result.negativeTests!) {
      expect(nt.flag).toBe('false');
    }
  });

  it('estimateModel with non-string values', () => {
    const stats = tsEstimateModel({
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'version', values: [1, 2, 3] },
      ],
    });
    expect(stats.parameterCount).toBe(2);
    expect(stats.totalValues).toBe(5);
    expect(stats.totalTuples).toBe(6);
  });

  it('determinism: same non-string values + seed = same output', () => {
    const input: WasmGenerateInput = {
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'version', values: [1, 2, 3] },
        { name: 'os', values: ['win', 'mac'] },
      ],
      seed: 42,
    };
    const r1 = tsGenerate(input);
    const r2 = tsGenerate(input);
    expect(r1.tests).toEqual(r2.tests);
  });
});
