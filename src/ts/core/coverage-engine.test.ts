import { EqualsNode, ImpliesNode, NotEqualsNode } from '../model/constraint-ast.js';
import { ErrorCode } from '../model/error.js';
import { Parameter, UNASSIGNED } from '../model/parameter.js';
import type { TestCase } from '../model/test-case.js';
import { CoverageEngine } from './coverage-engine.js';

describe('CoverageEngine', () => {
  describe('create()', () => {
    it('initializes with correct totalTuples for 2x2 params, strength=2', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      expect(result.error.code).toBe(ErrorCode.Ok);
      const engine = result.engine;
      // C(2,2) = 1 combination, 2*2 = 4 tuples
      expect(engine.totalTuples).toBe(4);
      expect(engine.isComplete).toBe(false);
    });

    it('returns TupleExplosion error when tuple count exceeds MAX_TUPLES', () => {
      // Create many parameters with large values and high strength to exceed 16M tuples.
      // 20 params x 10 values each, strength=5: C(20,5) * 10^5 = 15504 * 100000 > 16M
      const params: Parameter[] = [];
      for (let i = 0; i < 20; ++i) {
        const values: string[] = [];
        for (let j = 0; j < 10; ++j) {
          values.push(`v${j}`);
        }
        params.push(new Parameter(`p${i}`, values));
      }
      const result = CoverageEngine.create(params, 5);
      expect(result.error.code).toBe(ErrorCode.TupleExplosion);
      expect(result.error.message).toContain('tuple count exceeds safety limit');
    });

    it('returns TupleExplosion error for 10 params x 100 values at strength 5', () => {
      // 10 params x 100 values, strength=5: C(10,5) * 100^5 = 252 * 10^10 >> 16M
      const params: Parameter[] = [];
      for (let i = 0; i < 10; ++i) {
        const values: string[] = [];
        for (let j = 0; j < 100; ++j) {
          values.push(`v${j}`);
        }
        params.push(new Parameter(`p${i}`, values));
      }
      const result = CoverageEngine.create(params, 5);
      expect(result.error.code).toBe(ErrorCode.TupleExplosion);
      expect(result.error.message).toBeTruthy();
      expect(result.error.message.length).toBeGreaterThan(0);
      expect(result.error.detail).toBeTruthy();
    });
  });

  describe('addTestCase()', () => {
    it('increases coveredCount and updates coverageRatio', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      expect(result.error.code).toBe(ErrorCode.Ok);
      const engine = result.engine;

      engine.addTestCase({ values: [0, 0] }); // os=win, browser=chrome
      expect(engine.coveredCount).toBe(1);
      expect(engine.coverageRatio).toBeCloseTo(0.25);
      expect(engine.isComplete).toBe(false);
    });
  });

  describe('full coverage', () => {
    it('reaches isComplete=true and coverageRatio=1.0 for 2x2', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      const engine = result.engine;

      // All 4 pairs for 2 params x 2 values:
      engine.addTestCase({ values: [0, 0] }); // win, chrome
      engine.addTestCase({ values: [0, 1] }); // win, safari
      engine.addTestCase({ values: [1, 0] }); // mac, chrome
      engine.addTestCase({ values: [1, 1] }); // mac, safari

      expect(engine.isComplete).toBe(true);
      expect(engine.coverageRatio).toBe(1.0);
      expect(engine.coveredCount).toBe(4);
    });
  });

  describe('scoreCandidate()', () => {
    it('returns the number of new tuples a test case would cover', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      const engine = result.engine;

      // No tests yet, so a candidate should cover 1 tuple.
      const score1 = engine.scoreCandidate({ values: [0, 0] });
      expect(score1).toBe(1);

      // Add that test case.
      engine.addTestCase({ values: [0, 0] });

      // Same candidate now covers 0 new tuples.
      const score2 = engine.scoreCandidate({ values: [0, 0] });
      expect(score2).toBe(0);

      // A different candidate covers 1 new tuple.
      const score3 = engine.scoreCandidate({ values: [0, 1] });
      expect(score3).toBe(1);
    });
  });

  describe('scoreValue()', () => {
    it('returns score for a single value assignment given partial test case', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      const engine = result.engine;

      // Partial test case with browser=chrome assigned.
      const partial: TestCase = { values: [UNASSIGNED, 0] };

      // Scoring os=win (param 0, value 0) should find 1 uncovered tuple: (os=win, browser=chrome).
      const score = engine.scoreValue(partial, 0, 0);
      expect(score).toBe(1);

      // Add the test case that covers (win, chrome).
      engine.addTestCase({ values: [0, 0] });

      // Now scoring os=win with browser=chrome should give 0.
      const score2 = engine.scoreValue(partial, 0, 0);
      expect(score2).toBe(0);

      // But os=mac with browser=chrome should still give 1.
      const score3 = engine.scoreValue(partial, 0, 1);
      expect(score3).toBe(1);
    });
  });

  describe('excludeInvalidTuples()', () => {
    it('reduces totalTuples when constraints exclude some tuples', () => {
      // params: os={win, mac}, browser={chrome, ie, safari}
      // constraint: IF os=mac THEN browser!=ie
      // This means the tuple (os=mac, browser=ie) is invalid.
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'ie', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      const engine = result.engine;

      // Without constraints: C(2,2)*2*3 = 6 tuples.
      expect(engine.totalTuples).toBe(6);

      // IF os=mac THEN browser!=ie
      // os is param 0, mac is value 1. browser is param 1, ie is value 1.
      const constraint = new ImpliesNode(
        new EqualsNode(0, 1), // os=mac
        new NotEqualsNode(1, 1), // browser!=ie
      );
      engine.excludeInvalidTuples([constraint]);

      // (mac, ie) is excluded, so 6 - 1 = 5 valid tuples.
      expect(engine.totalTuples).toBe(5);
    });
  });

  describe('excludeInvalidValues()', () => {
    it('excludes tuples containing values marked as invalid', () => {
      // browser has 'ie' marked as invalid.
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'ie', 'safari'], [false, true, false]),
      ];
      const result = CoverageEngine.create(params, 2);
      const engine = result.engine;

      // Without exclusion: 2*3 = 6 tuples.
      expect(engine.totalTuples).toBe(6);

      engine.excludeInvalidValues();

      // Tuples with browser=ie excluded: (win, ie) and (mac, ie) -> 6 - 2 = 4.
      expect(engine.totalTuples).toBe(4);
    });
  });

  describe('getUncoveredTuples()', () => {
    it('returns correct uncovered tuple strings', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
      ];
      const result = CoverageEngine.create(params, 2);
      const engine = result.engine;

      // Cover only (win, chrome).
      engine.addTestCase({ values: [0, 0] });

      const uncovered = engine.getUncoveredTuples(params);
      expect(uncovered.length).toBe(3);

      // Collect all tuple strings for easier assertion.
      const tupleStrings = uncovered.map((ut) => ut.tuple.join(', '));
      expect(tupleStrings).toContain('os=win, browser=safari');
      expect(tupleStrings).toContain('os=mac, browser=chrome');
      expect(tupleStrings).toContain('os=mac, browser=safari');

      // Each uncovered tuple should have reason and params.
      for (const ut of uncovered) {
        expect(ut.reason).toBe('never covered');
        expect(ut.params).toEqual(['os', 'browser']);
      }
    });
  });

  describe('createFromSubset()', () => {
    it('tracks tuples only for the specified parameter subset', () => {
      const params = [
        new Parameter('os', ['win', 'mac']),
        new Parameter('browser', ['chrome', 'safari']),
        new Parameter('lang', ['en', 'ja', 'fr']),
      ];

      // Subset: only os and lang (indices 0 and 2).
      const result = CoverageEngine.createFromSubset(params, [0, 2], 2);
      expect(result.error.code).toBe(ErrorCode.Ok);
      const engine = result.engine;

      // C(2,2) = 1 combination, 2*3 = 6 tuples (os x lang only).
      expect(engine.totalTuples).toBe(6);

      // Adding a test covers tuples using global param indices.
      engine.addTestCase({ values: [0, 0, 0] }); // os=win, browser=chrome, lang=en
      expect(engine.coveredCount).toBe(1);

      // Adding all combinations of os x lang.
      engine.addTestCase({ values: [0, 0, 1] }); // win, *, ja
      engine.addTestCase({ values: [0, 0, 2] }); // win, *, fr
      engine.addTestCase({ values: [1, 0, 0] }); // mac, *, en
      engine.addTestCase({ values: [1, 0, 1] }); // mac, *, ja
      engine.addTestCase({ values: [1, 0, 2] }); // mac, *, fr

      expect(engine.isComplete).toBe(true);
      expect(engine.coverageRatio).toBe(1.0);
    });
  });

  describe('3 params x 3 values', () => {
    it('has totalTuples = C(3,2) * 3 * 3 = 27', () => {
      const params = [
        new Parameter('a', ['a1', 'a2', 'a3']),
        new Parameter('b', ['b1', 'b2', 'b3']),
        new Parameter('c', ['c1', 'c2', 'c3']),
      ];
      const result = CoverageEngine.create(params, 2);
      expect(result.error.code).toBe(ErrorCode.Ok);
      // C(3,2) = 3 combinations, each with 3*3 = 9 tuples -> 27 total.
      expect(result.engine.totalTuples).toBe(27);
    });
  });
});
