import { beforeAll, describe, expect, it } from 'vitest';
import {
  analyzeCoverage,
  estimateModel,
  extendTests,
  generate,
  init,
  type Parameter,
  type TestCase,
} from './index';

beforeAll(async () => {
  await init();
});

describe('generate()', () => {
  it('basic pairwise: 2 params × 2 values → full coverage', () => {
    const result = generate({
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(0);
    expect(result.uncovered).toHaveLength(0);
  });

  it('3 params × 3 values → full coverage', () => {
    const result = generate({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        { name: 'arch', values: ['x64', 'arm64', 'x86'] },
      ],
    });
    expect(result.coverage).toBe(1.0);
    expect(result.stats.totalTuples).toBe(27); // C(3,2) * 3*3 = 3 * 9
  });

  it('with constraints: IF os=mac THEN browser!=ie', () => {
    const result = generate({
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

  it('strength 3 → full coverage', () => {
    const result = generate({
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
      ],
      strength: 3,
    });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBe(8); // 2^3 exhaustive
  });

  it('determinism: same seed → same tests', () => {
    const input = {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      seed: 42,
    };
    const r1 = generate(input);
    const r2 = generate(input);
    expect(r1.tests).toEqual(r2.tests);
  });

  it('maxTests limits output count', () => {
    const result = generate({
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
    const result = generate({
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
    const result = generate({
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
    const result = generate({
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
    expect(result.negativeTests!.length).toBeGreaterThan(0);
    for (const nt of result.negativeTests!) {
      expect(nt.browser).toBe('ie6');
    }
  });

  it('with weights: higher weight values are preferred', () => {
    const counts: Record<string, number> = { win: 0, mac: 0, linux: 0 };
    // Run multiple seeds to observe weight effect
    for (let seed = 0; seed < 10; seed++) {
      const result = generate({
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
    // win should appear significantly more often due to high weight
    expect(counts.win).toBeGreaterThan(counts.mac);
  });
});

describe('analyzeCoverage()', () => {
  const params: Parameter[] = [
    { name: 'os', values: ['win', 'mac'] },
    { name: 'browser', values: ['chrome', 'firefox'] },
  ];

  it('full coverage → coverageRatio 1.0', () => {
    const tests: TestCase[] = [
      { os: 'win', browser: 'chrome' },
      { os: 'win', browser: 'firefox' },
      { os: 'mac', browser: 'chrome' },
      { os: 'mac', browser: 'firefox' },
    ];
    const report = analyzeCoverage(params, tests);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);
  });

  it('partial coverage → uncovered tuples present', () => {
    const tests: TestCase[] = [{ os: 'win', browser: 'chrome' }];
    const report = analyzeCoverage(params, tests);
    expect(report.coverageRatio).toBeLessThan(1.0);
    expect(report.uncovered.length).toBeGreaterThan(0);
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(1);
  });

  it('default strength is 2', () => {
    const tests: TestCase[] = [
      { os: 'win', browser: 'chrome' },
      { os: 'win', browser: 'firefox' },
      { os: 'mac', browser: 'chrome' },
      { os: 'mac', browser: 'firefox' },
    ];
    const report = analyzeCoverage(params, tests);
    // 2-wise for 2 params = 4 tuples
    expect(report.totalTuples).toBe(4);
  });
});

describe('extendTests()', () => {
  const params: Parameter[] = [
    { name: 'os', values: ['win', 'mac', 'linux'] },
    { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
  ];

  it('extends partial test suite → improved coverage', () => {
    const existing: TestCase[] = [{ os: 'win', browser: 'chrome' }];
    const result = extendTests(existing, { parameters: params });
    expect(result.coverage).toBe(1.0);
    expect(result.tests.length).toBeGreaterThan(1);
    expect(result.tests[0]).toEqual({ os: 'win', browser: 'chrome' });
  });

  it('extends already complete suite → keeps coverage', () => {
    const full = generate({ parameters: params });
    const result = extendTests(full.tests, { parameters: params });
    expect(result.coverage).toBe(1.0);
  });
});

describe('estimateModel()', () => {
  it('returns correct model statistics', () => {
    const stats = estimateModel({
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
    });
    expect(stats.parameterCount).toBe(2);
    expect(stats.totalValues).toBe(5);
    expect(stats.strength).toBe(2);
    expect(stats.totalTuples).toBe(6); // 3 * 2
    expect(stats.estimatedTests).toBeGreaterThan(0);
    expect(stats.subModelCount).toBe(0);
    expect(stats.constraintCount).toBe(0);
    expect(stats.parameters).toHaveLength(2);
    expect(stats.parameters[0]).toEqual({ name: 'os', valueCount: 3, invalidCount: 0 });
    expect(stats.parameters[1]).toEqual({ name: 'browser', valueCount: 2, invalidCount: 0 });
  });

  it('reports sub-model and constraint counts', () => {
    const stats = estimateModel({
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
