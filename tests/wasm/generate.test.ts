import { beforeAll, describe, expect, it } from 'vitest';
import {
  analyzeCoverage,
  CoverwiseError,
  estimateModel,
  extendTests,
  generate,
  init,
  type Parameter,
  type TestCase,
} from '../../js/index.js';

describe('coverwise WASM', () => {
  beforeAll(async () => {
    await init();
  });

  describe('generate', () => {
    it('generates pairwise tests with 100% coverage', () => {
      const result = generate({
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      });
      expect(result.coverage).toBe(1.0);
      expect(result.stats.totalTuples).toBe(9);
      expect(result.stats.coveredTuples).toBe(9);
      expect(result.tests.length).toBeGreaterThanOrEqual(9);
      expect(result.uncovered).toHaveLength(0);
    });

    it('produces deterministic output with same seed', () => {
      const input = {
        parameters: [
          { name: 'X', values: ['a', 'b', 'c'] },
          { name: 'Y', values: ['1', '2'] },
        ],
        seed: 12345,
      };
      const r1 = generate(input);
      const r2 = generate(input);
      expect(r1.tests).toEqual(r2.tests);
    });

    it('respects constraints', () => {
      const result = generate({
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari', 'ie'] },
        ],
        constraints: ['IF os = mac THEN browser != ie'],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      for (const tc of result.tests) {
        if (tc.os === 'mac') {
          expect(tc.browser).not.toBe('ie');
        }
      }
    });

    it('handles maxTests limitation', () => {
      const result = generate({
        parameters: [
          { name: 'a', values: ['1', '2', '3', '4'] },
          { name: 'b', values: ['1', '2', '3', '4'] },
        ],
        maxTests: 3,
        seed: 1,
      });
      expect(result.tests).toHaveLength(3);
      expect(result.coverage).toBeLessThan(1.0);
      expect(result.uncovered.length).toBeGreaterThan(0);
    });

    it('rejects invalid numeric options', () => {
      const input = {
        parameters: [
          { name: 'a', values: ['1', '2'] },
          { name: 'b', values: ['1', '2'] },
        ],
      };
      const expectInvalid = (fn: () => unknown, message: RegExp) => {
        try {
          fn();
          throw new Error('expected function to throw');
        } catch (err) {
          expect((err as { message?: string }).message ?? '').toMatch(message);
        }
      };

      expectInvalid(() => generate({ ...input, strength: 0 }), /Invalid strength/);
      expectInvalid(() => generate({ ...input, strength: 1.5 }), /Invalid strength/);
      expectInvalid(() => generate({ ...input, maxTests: -1 }), /Invalid maxTests/);
      expectInvalid(() => generate({ ...input, maxTests: 1.5 }), /Invalid maxTests/);
    });

    it('supports weights', () => {
      const result = generate({
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox'] },
        ],
        weights: { os: { win: 10, mac: 1, linux: 1 } },
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
    });

    it('supports sub-models', () => {
      const result = generate({
        parameters: [
          { name: 'A', values: ['1', '2', '3'] },
          { name: 'B', values: ['1', '2', '3'] },
          { name: 'C', values: ['1', '2', '3'] },
          { name: 'D', values: ['1', '2'] },
        ],
        strength: 2,
        subModels: [{ parameters: ['A', 'B', 'C'], strength: 3 }],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
    });

    it('rejects a non-positive sub-model strength at the binding boundary', () => {
      // The shared runtime boundary rejects malformed sub-model strengths before
      // they can be truncated into a no-op by any engine.
      const base = {
        parameters: [
          { name: 'A', values: ['1', '2', '3'] },
          { name: 'B', values: ['1', '2', '3'] },
          { name: 'C', values: ['1', '2', '3'] },
        ],
        seed: 1,
      };
      expect(() =>
        generate({ ...base, subModels: [{ parameters: ['A', 'B'], strength: 0 }] }),
      ).toThrow(/Invalid subModels\[0\]/);
      expect(() =>
        generate({ ...base, subModels: [{ parameters: ['A', 'B'], strength: 2.5 }] }),
      ).toThrow(/Invalid subModels\[0\]/);
    });

    it('generates negative tests for invalid values', () => {
      const result = generate({
        parameters: [
          { name: 'browser', values: ['chrome', 'safari', { value: 'ie6', invalid: true }] },
          { name: 'os', values: ['win', 'mac'] },
        ],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      expect(result.negativeTests).toBeDefined();
      expect(result.negativeTests?.length).toBeGreaterThan(0);
      for (const tc of result.negativeTests ?? []) {
        expect(tc.browser).toBe('ie6');
      }
    });

    // negativeCoverage is documented as part of the shipped GenerateResult, so a
    // regression that drops it or fills it inconsistently has to fail here.
    it('reports negative coverage whose covered and omitted tuples sum to the total', () => {
      const result = generate({
        parameters: [
          { name: 'A', values: ['a0', { value: 'bad', invalid: true }] },
          { name: 'B', values: ['b0', 'b1', 'b2'] },
          { name: 'C', values: ['c0', 'c1', 'c2'] },
          { name: 'D', values: ['d0', 'd1', 'd2'] },
        ],
        strength: 4,
        seed: 42,
      });

      const negative = result.negativeCoverage;
      expect(negative).toBeDefined();
      if (!negative) {
        return;
      }
      expect(negative.coveredTuples + negative.omittedTuples).toBe(negative.totalTuples);
      expect(negative.omittedTuples).toBe(0);
      expect(negative.coverageRatio).toBe(1);
    });

    it('keeps negative coverage self-consistent when maxTests truncates the suite', () => {
      const result = generate({
        parameters: [
          { name: 'A', values: ['a0', { value: 'bad', invalid: true }] },
          { name: 'B', values: ['b0', 'b1'] },
        ],
        strength: 2,
        maxTests: 3,
        seed: 42,
      });

      const negative = result.negativeCoverage;
      expect(negative).toBeDefined();
      if (!negative) {
        return;
      }
      // The cap stops negative generation part way, so the omitted count has to
      // account for the difference rather than staying at its default of zero.
      expect(negative.omittedTuples).toBeGreaterThan(0);
      expect(negative.coveredTuples + negative.omittedTuples).toBe(negative.totalTuples);
      expect(negative.coverageRatio).toBe(negative.coveredTuples / negative.totalTuples);
    });

    it('supports 3-wise generation', () => {
      const result = generate({
        parameters: [
          { name: 'A', values: ['1', '2', '3'] },
          { name: 'B', values: ['1', '2', '3'] },
          { name: 'C', values: ['1', '2', '3'] },
          { name: 'D', values: ['1', '2', '3'] },
        ],
        strength: 3,
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      // Verify more tests are generated than for 2-wise
      const result2 = generate({
        parameters: [
          { name: 'A', values: ['1', '2', '3'] },
          { name: 'B', values: ['1', '2', '3'] },
          { name: 'C', values: ['1', '2', '3'] },
          { name: 'D', values: ['1', '2', '3'] },
        ],
        strength: 2,
        seed: 1,
      });
      expect(result.tests.length).toBeGreaterThan(result2.tests.length);
    });
  });

  describe('analyzeCoverage', () => {
    it('reports full coverage for complete test suite', () => {
      const params: Parameter[] = [
        { name: 'a', values: ['0', '1'] },
        { name: 'b', values: ['0', '1'] },
      ];
      const tests: TestCase[] = [
        { a: '0', b: '0' },
        { a: '0', b: '1' },
        { a: '1', b: '0' },
        { a: '1', b: '1' },
      ];
      const report = analyzeCoverage(params, tests, 2);
      expect(report.coverageRatio).toBe(1.0);
      expect(report.uncovered).toHaveLength(0);
    });

    it('reports uncovered tuples for incomplete suite', () => {
      const params: Parameter[] = [
        { name: 'a', values: ['0', '1'] },
        { name: 'b', values: ['0', '1'] },
      ];
      const tests: TestCase[] = [{ a: '0', b: '0' }];
      const report = analyzeCoverage(params, tests, 2);
      expect(report.coverageRatio).toBeLessThan(1.0);
      expect(report.uncovered.length).toBeGreaterThan(0);
    });
  });

  describe('extendTests', () => {
    it('extends existing tests to achieve full coverage', () => {
      const input = {
        parameters: [
          { name: 'a', values: ['0', '1'] },
          { name: 'b', values: ['0', '1'] },
          { name: 'c', values: ['0', '1'] },
        ],
        seed: 1,
      };
      const existing: TestCase[] = [{ a: '0', b: '0', c: '0' }];
      const result = extendTests(existing, input);
      expect(result.coverage).toBe(1.0);
      expect(result.tests[0]).toEqual(existing[0]);
    });

    it("accepts the supported 'strict' mode", () => {
      const input = {
        parameters: [
          { name: 'a', values: ['0', '1'] },
          { name: 'b', values: ['0', '1'] },
        ],
        mode: 'strict' as const,
        seed: 1,
      };
      const existing: TestCase[] = [{ a: '0', b: '0' }];
      const result = extendTests(existing, input);
      expect(result.coverage).toBe(1.0);
      expect(result.tests[0]).toEqual(existing[0]);
    });

    it('rejects an unsupported extend mode with CoverwiseError(INVALID_INPUT)', () => {
      const input = {
        parameters: [
          { name: 'a', values: ['0', '1'] },
          { name: 'b', values: ['0', '1'] },
        ],
        // Cast: 'relaxed' is not a valid ExtendInput['mode'] at the type level.
        mode: 'relaxed' as unknown as 'strict',
        seed: 1,
      };
      const existing: TestCase[] = [{ a: '0', b: '0' }];
      let thrown: unknown;
      try {
        extendTests(existing, input);
      } catch (e) {
        thrown = e;
      }
      expect(thrown).toBeInstanceOf(CoverwiseError);
      expect((thrown as CoverwiseError).code).toBe('INVALID_INPUT');
    });
  });

  describe('error surface', () => {
    it('throws CoverwiseError (with .code) from the public validation path', () => {
      const badInput = {
        parameters: [
          { name: 'a', values: ['0', '1'] },
          { name: 'b', values: ['0', '1'] },
        ],
        // Non-array tests must be rejected before reaching the WASM module.
        seed: 1,
      };
      let thrown: unknown;
      try {
        // A non-array `tests` argument must throw CoverwiseError, not a raw TypeError.
        analyzeCoverage(badInput.parameters, 'not-an-array' as unknown as TestCase[], 2);
      } catch (e) {
        thrown = e;
      }
      expect(thrown).toBeInstanceOf(CoverwiseError);
      expect(thrown).toBeInstanceOf(Error);
      expect((thrown as CoverwiseError).code).toBe('INVALID_INPUT');
    });
  });

  describe('estimateModel', () => {
    it('returns model statistics', () => {
      const stats = estimateModel({
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox'] },
        ],
      });
      expect(stats.parameterCount).toBe(2);
      expect(stats.totalValues).toBe(5);
      expect(stats.totalTuples).toBe(6);
      expect(stats.estimatedTests).toBeGreaterThan(0);
    });
  });

  describe('Japanese and emoji support', () => {
    it('generates with Japanese parameter names and constraints (unquoted)', () => {
      const result = generate({
        parameters: [
          { name: 'OS', values: ['win', 'mac', 'linux'] },
          { name: 'ブラウザ', values: ['chrome', 'safari', 'edge'] },
        ],
        constraints: ['IF OS = mac THEN ブラウザ != edge'],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      for (const tc of result.tests) {
        if (tc.OS === 'mac') {
          expect(tc.ブラウザ).not.toBe('edge');
        }
      }
    });

    it('constraints with quoted Japanese values', () => {
      const result = generate({
        parameters: [
          { name: 'OS', values: ['ウィンドウズ', 'マック', 'リナックス'] },
          { name: 'ブラウザ', values: ['クローム', 'サファリ', 'エッジ'] },
        ],
        constraints: ['IF OS = "マック" THEN ブラウザ != "エッジ"'],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      for (const tc of result.tests) {
        if (tc.OS === 'マック') {
          expect(tc.ブラウザ).not.toBe('エッジ');
        }
      }
    });

    it('constraints with values containing spaces (quoted)', () => {
      const result = generate({
        parameters: [
          { name: 'OS', values: ['Windows 10', 'macOS', 'Ubuntu'] },
          { name: 'browser', values: ['chrome', 'edge'] },
        ],
        constraints: ['IF OS = "Windows 10" THEN browser != "edge"'],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      for (const tc of result.tests) {
        if (tc.OS === 'Windows 10') {
          expect(tc.browser).not.toBe('edge');
        }
      }
    });

    it('analyzeCoverage with Japanese parameter names', () => {
      const params: Parameter[] = [
        { name: 'OS', values: ['win', 'mac'] },
        { name: 'ブラウザ', values: ['chrome', 'firefox'] },
      ];
      const tests: TestCase[] = [
        { OS: 'win', ブラウザ: 'chrome' },
        { OS: 'win', ブラウザ: 'firefox' },
        { OS: 'mac', ブラウザ: 'chrome' },
        { OS: 'mac', ブラウザ: 'firefox' },
      ];
      const report = analyzeCoverage(params, tests, 2);
      expect(report.coverageRatio).toBe(1.0);
      expect(report.uncovered).toHaveLength(0);
    });

    it('generates with emoji parameter names', () => {
      const result = generate({
        parameters: [
          { name: '🖥️', values: ['💻', '🖥️'] },
          { name: '🌐', values: ['🔥', '🧊'] },
        ],
        seed: 1,
      });
      expect(result.coverage).toBe(1.0);
      for (const tc of result.tests) {
        expect(tc).toHaveProperty('🖥️');
        expect(tc).toHaveProperty('🌐');
      }
    });

    it('analyzeCoverage with emoji keys', () => {
      const params: Parameter[] = [
        { name: '🎯', values: ['hit', 'miss'] },
        { name: '🎲', values: ['1', '2'] },
      ];
      const tests: TestCase[] = [
        { '🎯': 'hit', '🎲': '1' },
        { '🎯': 'miss', '🎲': '2' },
        { '🎯': 'hit', '🎲': '2' },
        { '🎯': 'miss', '🎲': '1' },
      ];
      const report = analyzeCoverage(params, tests, 2);
      expect(report.coverageRatio).toBe(1.0);
    });
  });
});
