import type { ConstraintNode } from '../model/constraint-ast.js';
import {
  ConstraintResult,
  EqualsNode,
  ImpliesNode,
  NotEqualsNode,
} from '../model/constraint-ast.js';
import { UNASSIGNED } from '../model/parameter.js';
import { Rng } from '../util/rng.js';
import { greedyConstruct, type ScoreFn } from './greedy.js';

describe('greedyConstruct', () => {
  it('returns a test case with all values assigned', () => {
    const params = [{ size: 3 }, { size: 2 }];
    const scoreFn: ScoreFn = () => 1;
    const rng = new Rng(42);
    const tc = greedyConstruct(params, scoreFn, [], rng);

    expect(tc.values.length).toBe(2);
    expect(tc.values[0]).not.toBe(UNASSIGNED);
    expect(tc.values[1]).not.toBe(UNASSIGNED);
    expect(tc.values[0]).toBeGreaterThanOrEqual(0);
    expect(tc.values[0]).toBeLessThan(3);
    expect(tc.values[1]).toBeGreaterThanOrEqual(0);
    expect(tc.values[1]).toBeLessThan(2);
  });

  it('uses scoreFn to pick higher-scoring values', () => {
    // Single param with 3 values. Score function strongly prefers value 2.
    const params = [{ size: 3 }];
    const scoreFn: ScoreFn = (_partial, _pi, vi) => {
      return vi === 2 ? 100 : 0;
    };
    const rng = new Rng(42);
    const tc = greedyConstruct(params, scoreFn, [], rng);

    expect(tc.values[0]).toBe(2);
  });

  it('skips values pruned by constraints', () => {
    // 2 params: os={win, mac}, browser={chrome, ie, safari}
    // Constraint: IF os=mac THEN browser!=ie
    // If os is assigned to mac first, ie should be pruned for browser.
    const params = [{ size: 2 }, { size: 3 }];

    // Score function that always picks mac (value 1) for os and ie (value 1) for browser.
    const scoreFn: ScoreFn = (_partial, _pi, vi) => {
      return vi === 1 ? 100 : 0;
    };

    // Constraint: IF param0=1 (mac) THEN param1!=1 (ie)
    const constraint = new ImpliesNode(new EqualsNode(0, 1), new NotEqualsNode(1, 1));

    const rng = new Rng(42);
    const tc = greedyConstruct(params, scoreFn, [constraint], rng);

    // os should be mac (value 1) since it's preferred.
    // browser should NOT be ie (value 1) because it's pruned.
    if (tc.values[0] === 1) {
      // mac was assigned
      expect(tc.values[1]).not.toBe(1); // ie must be pruned
    }
    // Either way, all values must be assigned.
    expect(tc.values[0]).not.toBe(UNASSIGNED);
    expect(tc.values[1]).not.toBe(UNASSIGNED);
  });

  it('respects allowedValues mask', () => {
    // 2 params with 4 values each. Only allow values 0 and 2 for param 0,
    // and only value 3 for param 1.
    const params = [{ size: 4 }, { size: 4 }];
    const scoreFn: ScoreFn = () => 1;
    const allowedValues = [
      [true, false, true, false], // param 0: only 0 and 2
      [false, false, false, true], // param 1: only 3
    ];
    const rng = new Rng(42);
    const tc = greedyConstruct(params, scoreFn, [], rng, allowedValues);

    expect(tc.values[0] === 0 || tc.values[0] === 2).toBe(true);
    expect(tc.values[1]).toBe(3);
  });

  it('falls back to first allowed value when all values are pruned by constraints', () => {
    // Single param with 3 values. Constraint rejects all values.
    const params = [{ size: 3 }];
    const scoreFn: ScoreFn = () => 1;

    // A constraint that always returns False for any assigned value.
    const alwaysFalse: ConstraintNode = {
      evaluate(assignment: number[]): ConstraintResult {
        if (assignment[0] === UNASSIGNED) {
          return ConstraintResult.Unknown;
        }
        return ConstraintResult.False;
      },
    };

    const allowedValues = [[true, true, false]]; // values 0 and 1 allowed
    const rng = new Rng(42);
    const tc = greedyConstruct(params, scoreFn, [alwaysFalse], rng, allowedValues);

    // When all allowed values are pruned by constraint, fallback to first allowed.
    expect(tc.values[0]).toBe(0);
  });

  it('falls back to 0 when no allowed mask and all values pruned', () => {
    const params = [{ size: 3 }];
    const scoreFn: ScoreFn = () => 1;

    const alwaysFalse: ConstraintNode = {
      evaluate(assignment: number[]): ConstraintResult {
        if (assignment[0] === UNASSIGNED) {
          return ConstraintResult.Unknown;
        }
        return ConstraintResult.False;
      },
    };

    const rng = new Rng(42);
    const tc = greedyConstruct(params, scoreFn, [alwaysFalse], rng);

    // No mask, all pruned -> fallback to 0.
    expect(tc.values[0]).toBe(0);
  });

  it('produces deterministic results with the same seed', () => {
    const params = [{ size: 5 }, { size: 4 }, { size: 3 }];
    const scoreFn: ScoreFn = () => 1; // All equal scores, RNG decides.

    const rng1 = new Rng(77);
    const tc1 = greedyConstruct(params, scoreFn, [], rng1);

    const rng2 = new Rng(77);
    const tc2 = greedyConstruct(params, scoreFn, [], rng2);

    expect(tc1.values).toEqual(tc2.values);
  });

  it('produces different results with different seeds', () => {
    const params = [{ size: 10 }, { size: 10 }, { size: 10 }];
    const scoreFn: ScoreFn = () => 1;

    const rng1 = new Rng(1);
    const tc1 = greedyConstruct(params, scoreFn, [], rng1);

    const rng2 = new Rng(9999);
    const tc2 = greedyConstruct(params, scoreFn, [], rng2);

    // With 10 values per param and equal scores, different seeds should produce
    // different test cases with very high probability.
    const same = tc1.values.every((v, i) => v === tc2.values[i]);
    expect(same).toBe(false);
  });
});
