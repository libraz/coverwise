import { beforeAll, describe, expect, it } from 'vitest';
import {
  analyzeCoverage,
  estimateModel,
  extendTests,
  generate,
  init,
  type Parameter,
  type TestCase,
} from '../../js/index';

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
});
