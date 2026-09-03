import { EqualsNode, ImpliesNode, NotEqualsNode } from '../model/constraint-ast.js';
import { ErrorCode } from '../model/error.js';
import { Parameter, UNASSIGNED } from '../model/parameter.js';
import type { TestCase } from '../model/test-case.js';
import { MAX_DIAGNOSTIC_TUPLES } from '../model/tuning-limits.js';
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

    it('tuple explosion error message matches C++', () => {
      // A model over the tuple limit must surface a structured TupleExplosion
      // error (never throw a raw Error mid-computation), with the exact same code
      // and message as the C++ surface (see coverage_engine_test.cpp:
      // TupleExplosionErrorMessageParity).
      const params: Parameter[] = [];
      for (let i = 0; i < 10; ++i) {
        const values: string[] = [];
        for (let j = 0; j < 100; ++j) {
          values.push(`v${j}`);
        }
        params.push(new Parameter(`p${i}`, values));
      }
      // create() must not throw; it returns the structured error instead.
      const result = CoverageEngine.create(params, 5);
      expect(result.error.code).toBe(ErrorCode.TupleExplosion);
      expect(result.error.message).toBe('t-wise tuple count exceeds safety limit');
      // The detail reports the real magnitude — a single combination's product is
      // 100^5 = 10,000,000,000 — not a fixed sentinel just past the limit.
      expect(result.error.detail).toContain('10000000000');
      expect(result.error.detail).not.toContain('16000001');
    });

    it('near-threshold model just under MAX_TUPLES succeeds without spurious explosion', () => {
      // C(8,2) = 28 combinations, each 100*100 = 10000 tuples => 280000 total,
      // far below the 16M limit. Exercises the 64-bit accumulation path with a
      // value product that would matter for wider models, and must report the
      // exact tuple count with no explosion error.
      const params: Parameter[] = [];
      for (let i = 0; i < 8; ++i) {
        const values: string[] = [];
        for (let j = 0; j < 100; ++j) {
          values.push(`v${j}`);
        }
        params.push(new Parameter(`p${i}`, values));
      }
      const result = CoverageEngine.create(params, 2);
      expect(result.error.code).toBe(ErrorCode.Ok);
      // C(8,2) = 28, each 100*100 = 10000 -> 280000.
      expect(result.engine.totalTuples).toBe(280000);
    });

    it('rejects combination metadata before materializing it', () => {
      const params = Array.from({ length: 200 }, (_, i) => new Parameter(`P${i}`, ['only']));
      const result = CoverageEngine.create(params, 3);
      expect(result.error.code).toBe(ErrorCode.TupleExplosion);
      expect(result.error.message).toBe('parameter combination metadata exceeds safety limit');
    });

    // Both figures below are what the C++ core reports for the same model; the
    // cross-surface test reads them off the compiled module rather than from
    // here, and these pin them in the tier that runs without a WASM build.
    it('reports a tuple count that has left the exact range of a double', () => {
      // 3^34 is odd and above 2^53, so a double cannot hold it: rounding it
      // would report ...568 for a model the core describes as ...569.
      const params = Array.from({ length: 34 }, (_, i) => new Parameter(`p${i}`, ['a', 'b', 'c']));
      const result = CoverageEngine.create(params, 34);
      expect(result.error.code).toBe(ErrorCode.TupleExplosion);
      expect(result.error.detail).toBe(
        'Total tuples: 16677181699666569, limit: 16000000. Reduce strength or parameter count.',
      );
    });

    it('saturates the reported tuple count where the count stops being uint64', () => {
      // 64^11 = 2^66 is past what a uint64 holds, so the count the core reports
      // is the ceiling it saturates at rather than the true product.
      const values = Array.from({ length: 64 }, (_, j) => `v${j}`);
      const params = Array.from({ length: 11 }, (_, i) => new Parameter(`p${i}`, values));
      const result = CoverageEngine.create(params, 11);
      expect(result.error.code).toBe(ErrorCode.TupleExplosion);
      expect(result.error.detail).toBe(
        'Total tuples: 18446744073709551615, limit: 16000000. Reduce strength or parameter count.',
      );
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

    it('scales across many simple constrained parameters', () => {
      const parameterCount = 1500;
      const params = Array.from(
        { length: parameterCount },
        (_, index) => new Parameter(`P${index}`, ['v']),
      );
      const result = CoverageEngine.create(params, 1);
      const budgetExceeded = { value: false };

      result.engine.excludeInvalidTuples([new EqualsNode(0, 0)], [], budgetExceeded);

      expect(budgetExceeded.value).toBe(false);
      expect(result.engine.totalTuples).toBe(parameterCount);
    }, 3000);
  });

  describe('excludeTuplesOutsideMask()', () => {
    // 3 binary parameters at strength 2: C(3,2) = 3 combinations x 4 value pairs.
    const binary3 = () => [
      new Parameter('A', ['0', '1']),
      new Parameter('B', ['0', '1']),
      new Parameter('C', ['0', '1']),
    ];

    it('keeps only the values the mask allows', () => {
      const engine = CoverageEngine.create(binary3(), 2).engine;
      expect(engine.totalTuples).toBe(12);

      engine.excludeTuplesOutsideMask([
        [true, false],
        [true, true],
        [true, true],
      ]);

      // A=1 is disallowed, which removes half of the (A,B) and (A,C) tuples.
      expect(engine.totalTuples).toBe(8);
    });

    it('excludes everything when the mask does not describe the model', () => {
      // A mask the engine cannot read must not leave the caller holding a tuple
      // set it believes was filtered. Both ways of failing to describe the model
      // — a different number of rows than there are parameters, and a row whose
      // length differs from its parameter's domain — have to refuse in the same
      // direction.
      const outer = CoverageEngine.create(binary3(), 2).engine;
      expect(outer.totalTuples).toBe(12);
      outer.excludeTuplesOutsideMask([
        [true, true],
        [true, true],
      ]);
      expect(outer.totalTuples).toBe(0);
      expect(outer.isComplete).toBe(true);

      const inner = CoverageEngine.create(binary3(), 2).engine;
      expect(inner.totalTuples).toBe(12);
      inner.excludeTuplesOutsideMask([
        [true, true, true],
        [true, true, true],
        [true, true, true],
      ]);
      expect(inner.totalTuples).toBe(0);
      expect(inner.isComplete).toBe(true);
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

    it('stops at the diagnostic budget however many tuples are missing', () => {
      const params = Array.from(
        { length: 10 },
        (_, pi) =>
          new Parameter(
            `p${pi}`,
            Array.from({ length: 16 }, (_, vi) => String(vi)),
          ),
      );
      const engine = CoverageEngine.create(params, 2).engine;

      // The readable list is bounded, while the walk sees every missing tuple.
      expect(engine.totalTuples).toBeGreaterThan(MAX_DIAGNOSTIC_TUPLES);
      expect(engine.getUncoveredTuples(params)).toHaveLength(MAX_DIAGNOSTIC_TUPLES);
      let visited = 0;
      engine.forEachUncoveredTuple(() => {
        ++visited;
        return true;
      });
      expect(visited).toBe(engine.totalTuples);
    });
  });

  describe('needsTuple()', () => {
    it('answers for a tuple another engine enumerated', () => {
      const params = [
        new Parameter('A', ['a0', 'a1']),
        new Parameter('B', ['b0', 'b1']),
        new Parameter('C', ['c0', 'c1']),
      ];
      const global = CoverageEngine.create(params, 2).engine;
      const subAb = CoverageEngine.createFromSubset(params, [0, 1], 2).engine;

      global.addTestCase({ values: [0, 0, 0] });

      subAb.forEachUncoveredTuple((combo, valueIndices) => {
        const coveredByTheTest = valueIndices[0] === 0 && valueIndices[1] === 0;
        expect(global.needsTuple(combo, valueIndices)).toBe(!coveredByTheTest);
        return true;
      });

      // A parameter combination outside the subset engine is not its tuple,
      // even while the global engine still needs it.
      expect(global.needsTuple([1, 2], [1, 1])).toBe(true);
      expect(subAb.needsTuple([1, 2], [1, 1])).toBe(false);
      // A tuple of a size the engine does not enumerate is never one of its own.
      expect(global.needsTuple([0, 1, 2], [1, 1, 1])).toBe(false);
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

  describe('firstUncovered()', () => {
    /// Drive the deterministic completion loop the generator uses: repeatedly
    /// cover the first uncovered tuple, filling the positions the tuple leaves
    /// unassigned with each parameter's first value.
    function driveCompletion(
      engine: CoverageEngine,
      params: Parameter[],
    ): { produced: string[]; calls: number } {
      const produced: string[] = [];
      let calls = 0;
      while (!engine.isComplete) {
        ++calls;
        const uncovered = engine.firstUncovered();
        if (uncovered === null) {
          break;
        }
        const values = uncovered.assignment.map((v) => (v === UNASSIGNED ? 0 : v));
        engine.addTestCase({ values });
        produced.push(params.map((p, pi) => `${p.name}=${p.values[values[pi]]}`).join(','));
      }
      return { produced, calls };
    }

    it('produces a stable completion order for a model with invalid values', () => {
      // Invalid values leave large excluded regions in the coverage bitmap, which
      // is exactly where the incremental scan must not change which tuple is
      // picked. The pinned suite matches the C++ surface (see
      // coverage_engine_test.cpp: CompletionOrderIsStableForModelWithInvalidValues).
      const params = [
        new Parameter('os', ['win', 'mac', 'beos'], [false, false, true]),
        new Parameter('browser', ['chrome', 'safari']),
        new Parameter('auth', ['oauth', 'basic', 'none'], [false, false, true]),
        new Parameter('region', ['us', 'eu']),
      ];
      const engine = CoverageEngine.create(params, 2).engine;
      engine.excludeInvalidValues();

      const { produced } = driveCompletion(engine, params);

      expect(produced).toEqual([
        'os=win,browser=chrome,auth=oauth,region=us',
        'os=win,browser=safari,auth=oauth,region=us',
        'os=mac,browser=chrome,auth=oauth,region=us',
        'os=mac,browser=safari,auth=oauth,region=us',
        'os=win,browser=chrome,auth=basic,region=us',
        'os=mac,browser=chrome,auth=basic,region=us',
        'os=win,browser=chrome,auth=oauth,region=eu',
        'os=mac,browser=chrome,auth=oauth,region=eu',
        'os=win,browser=safari,auth=basic,region=us',
        'os=win,browser=safari,auth=oauth,region=eu',
        'os=win,browser=chrome,auth=basic,region=eu',
      ]);
      expect(engine.isComplete).toBe(true);
    });

    it('rewinds after resetCoverage()', () => {
      // The scan may only skip a prefix while coverage bits are monotonically
      // set. resetCoverage() clears them, so the next scan must start over from
      // the very first tuple.
      const params = [
        new Parameter('a', ['0', '1']),
        new Parameter('b', ['0', '1']),
        new Parameter('c', ['0', '1']),
      ];
      const engine = CoverageEngine.create(params, 2).engine;

      const first = engine.firstUncovered();
      expect(first).not.toBeNull();

      driveCompletion(engine, params);
      expect(engine.isComplete).toBe(true);

      engine.resetCoverage();
      expect(engine.firstUncovered()).toEqual(first);
    });

    it('keeps the scan cost linear over a completion pass', () => {
      // 8 params x 4 values at strength 3: C(8,3) = 56 combinations x 64 value
      // tuples = 3584 tuple indices. The last value of every parameter is
      // invalid, so exclusion sets most of those bits up front and the completion
      // loop scans across long excluded stretches -- the case that made a
      // from-scratch rescan quadratic.
      const universe = 56 * 64;
      const params = Array.from(
        { length: 8 },
        (_, pi) => new Parameter(`p${pi}`, ['v0', 'v1', 'v2', 'bad'], [false, false, false, true]),
      );
      const engine = CoverageEngine.create(params, 3).engine;
      expect(engine.totalTuples).toBe(universe);
      engine.excludeInvalidValues();

      const { calls } = driveCompletion(engine, params);
      expect(engine.isComplete).toBe(true);
      expect(calls).toBeGreaterThan(10);

      // Every tuple index is examined at most once across the whole pass, plus
      // the one uncovered index each call stops on.
      expect(engine.scanBitTests).toBeLessThanOrEqual(universe + calls);
      // Restarting each scan at index 0 would test on the order of half the
      // universe per call; this keeps that regression from passing unnoticed.
      expect(engine.scanBitTests).toBeLessThan((calls * universe) / 4);
    });
  });

  describe('addValueScores()', () => {
    it('agrees with scoring each value on its own', () => {
      const params = [
        new Parameter('A', ['a0', 'a1', 'a2']),
        new Parameter('B', ['b0', 'b1']),
        new Parameter('C', ['c0', 'c1', 'c2', 'c3']),
      ];
      const { engine, error } = CoverageEngine.create(params, 2);
      expect(error.code).toBe(ErrorCode.Ok);
      engine.addTestCase({ values: [0, 0, 0] });
      engine.addTestCase({ values: [1, 1, 2] });

      // Partial assignments covering the interesting shapes: nothing else
      // assigned, one neighbour assigned, and every neighbour assigned.
      const partials: TestCase[] = [
        { values: [UNASSIGNED, UNASSIGNED, UNASSIGNED] },
        { values: [UNASSIGNED, 1, UNASSIGNED] },
        { values: [UNASSIGNED, 0, 3] },
      ];
      for (const partial of partials) {
        for (let pi = 0; pi < params.length; ++pi) {
          if (partial.values[pi] !== UNASSIGNED) {
            continue;
          }
          const batched = new Array<number>(params[pi].size).fill(0);
          engine.addValueScores(partial, pi, batched);
          for (let vi = 0; vi < params[pi].size; ++vi) {
            expect(batched[vi]).toBe(engine.scoreValue(partial, pi, vi));
          }
        }
      }
    });

    it('costs the same number of combination visits at any value count', () => {
      // Scoring one value at a time re-walks the parameter's combinations once
      // per value. The visit counter must stay at one walk per call, so a
      // parameter with ten times the values costs the same number of visits.
      const visitsFor = (valueCount: number): number => {
        const params = Array.from(
          { length: 4 },
          (_, pi) =>
            new Parameter(
              `P${pi}`,
              Array.from({ length: valueCount }, (_, vi) => `v${vi}`),
            ),
        );
        const { engine, error } = CoverageEngine.create(params, 2);
        expect(error.code).toBe(ErrorCode.Ok);
        const scores = new Array<number>(valueCount).fill(0);
        engine.addValueScores({ values: [UNASSIGNED, 0, 0, 0] }, 0, scores);
        return engine.valueScoreComboVisits;
      };

      const few = visitsFor(2);
      expect(few).toBeGreaterThan(0);
      expect(visitsFor(20)).toBe(few);
    });
  });

  describe('strength 0', () => {
    it('yields an empty tuple universe from both factories', () => {
      // Strength 0 selects no parameters, so there is no combination stride to
      // divide by and no tuple to cover. Both factories must agree on that
      // instead of evaluating an undefined expression.
      const params = [new Parameter('A', ['a0', 'a1']), new Parameter('B', ['b0', 'b1'])];

      const { engine, error } = CoverageEngine.create(params, 0);
      expect(error.code).toBe(ErrorCode.Ok);
      expect(engine.totalTuples).toBe(0);
      expect(engine.coveredCount).toBe(0);
      expect(engine.coverageRatio).toBe(1);
      expect(engine.isComplete).toBe(true);
      expect(engine.firstUncovered()).toBeNull();
      engine.addTestCase({ values: [0, 0] });
      expect(engine.coveredCount).toBe(0);
      expect(engine.getUncoveredTuples(params)).toEqual([]);

      const sub = CoverageEngine.createFromSubset(params, [0], 0);
      expect(sub.error.code).toBe(ErrorCode.Ok);
      expect(sub.engine.totalTuples).toBe(0);
      expect(sub.engine.isComplete).toBe(true);
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
