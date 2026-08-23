import type { ConstraintNode } from '../model/constraint-ast.js';
import {
  ConstraintResult,
  EqualsNode,
  ImpliesNode,
  NotEqualsNode,
} from '../model/constraint-ast.js';
import { UNASSIGNED } from '../model/parameter.js';
import type { TestCase } from '../model/test-case.js';
import { Rng } from '../util/rng.js';
import {
  createGreedyScratch,
  type GreedyParam,
  greedyConstruct,
  type ScoreValuesFn,
} from './greedy.js';

/// Score every value the same, so the RNG decides.
const uniform: ScoreValuesFn = (_partial, _pi, scores) => {
  for (let vi = 0; vi < scores.length; ++vi) {
    scores[vi] += 1;
  }
};

/// Score `preferred` far above every other value of every parameter.
function prefers(preferred: number): ScoreValuesFn {
  return (_partial, _pi, scores) => {
    for (let vi = 0; vi < scores.length; ++vi) {
      scores[vi] += vi === preferred ? 100 : 0;
    }
  };
}

/// Run one construction with a throwaway scratch buffer.
///
/// Production code reuses a single scratch across a whole generation pass; these
/// tests drive individual constructions, so each call gets its own.
function construct(
  params: readonly GreedyParam[],
  scoreValues: ScoreValuesFn,
  constraints: readonly ConstraintNode[],
  rng: Rng,
  allowedValues: boolean[][] = [],
  weights: number[][] = [],
): TestCase | null {
  const built = greedyConstruct(
    params,
    scoreValues,
    constraints,
    rng,
    createGreedyScratch(params),
    allowedValues,
    weights,
  );
  return built === null ? null : built.testCase;
}

describe('greedyConstruct', () => {
  it('returns a test case with all values assigned', () => {
    const params = [{ size: 3 }, { size: 2 }];
    const rng = new Rng(42);
    const tc = construct(params, uniform, [], rng);
    expect(tc).not.toBeNull();
    if (tc === null) {
      return;
    }

    expect(tc.values.length).toBe(2);
    expect(tc.values[0]).not.toBe(UNASSIGNED);
    expect(tc.values[1]).not.toBe(UNASSIGNED);
    expect(tc.values[0]).toBeGreaterThanOrEqual(0);
    expect(tc.values[0]).toBeLessThan(3);
    expect(tc.values[1]).toBeGreaterThanOrEqual(0);
    expect(tc.values[1]).toBeLessThan(2);
  });

  it('uses the scoring callback to pick higher-scoring values', () => {
    // Single param with 3 values. Score function strongly prefers value 2.
    const params = [{ size: 3 }];
    const rng = new Rng(42);
    const tc = construct(params, prefers(2), [], rng);
    expect(tc).not.toBeNull();
    if (tc === null) {
      return;
    }

    expect(tc.values[0]).toBe(2);
  });

  it('handles finite extreme tie weights without overflow', () => {
    const params = [{ size: 2 }];
    const selected = new Set<number>();
    for (let seed = 0; seed < 32; ++seed) {
      const tc = construct(
        params,
        uniform,
        [],
        new Rng(seed),
        [],
        [[Number.MAX_VALUE, Number.MAX_VALUE]],
      );
      expect(tc).not.toBeNull();
      selected.add(tc?.values[0] ?? -1);
    }
    expect(selected).toEqual(new Set([0, 1]));
  });

  it('skips values pruned by constraints', () => {
    // 2 params: os={win, mac}, browser={chrome, ie, safari}
    // Constraint: IF os=mac THEN browser!=ie
    // If os is assigned to mac first, ie should be pruned for browser.
    const params = [{ size: 2 }, { size: 3 }];

    // Score function that always picks mac (value 1) for os and ie (value 1) for browser.
    // Constraint: IF param0=1 (mac) THEN param1!=1 (ie)
    const constraint = new ImpliesNode(new EqualsNode(0, 1), new NotEqualsNode(1, 1));

    const rng = new Rng(42);
    const tc = construct(params, prefers(1), [constraint], rng);
    expect(tc).not.toBeNull();
    if (tc === null) {
      return;
    }

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
    const allowedValues = [
      [true, false, true, false], // param 0: only 0 and 2
      [false, false, false, true], // param 1: only 3
    ];
    const rng = new Rng(42);
    const tc = construct(params, uniform, [], rng, allowedValues);
    expect(tc).not.toBeNull();
    if (tc === null) {
      return;
    }

    expect(tc.values[0] === 0 || tc.values[0] === 2).toBe(true);
    expect(tc.values[1]).toBe(3);
  });

  it('fails construction when all allowed values are pruned by constraints', () => {
    // Single param with 3 values. Constraint rejects all values.
    const params = [{ size: 3 }];

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
    const tc = construct(params, uniform, [alwaysFalse], rng, allowedValues);

    // No constraint-satisfying value exists => construction fails (null) rather
    // than emitting a constraint-violating value.
    expect(tc).toBeNull();
  });

  it('fails construction when no allowed mask and all values are pruned', () => {
    const params = [{ size: 3 }];

    const alwaysFalse: ConstraintNode = {
      evaluate(assignment: number[]): ConstraintResult {
        if (assignment[0] === UNASSIGNED) {
          return ConstraintResult.Unknown;
        }
        return ConstraintResult.False;
      },
    };

    const rng = new Rng(42);
    const tc = construct(params, uniform, [alwaysFalse], rng);

    // No constraint-satisfying value exists => construction fails (null).
    expect(tc).toBeNull();
  });

  it('evaluates a fully pruned parameter once per value before failing', () => {
    // Every value allowed by the mask that survives the constraints is recorded
    // by the selection loop, and constraint evaluation is a pure function of the
    // same partial assignment, so re-running it could not rescue a parameter
    // with no usable value. Construction must fail after a single pass rather
    // than paying for a second one on every failed build.
    const params = [{ size: 4 }];
    let evaluations = 0;
    const countingRejectAll: ConstraintNode = {
      evaluate(): ConstraintResult {
        ++evaluations;
        return ConstraintResult.False;
      },
    };

    expect(construct(params, uniform, [countingRejectAll], new Rng(3))).toBeNull();
    expect(evaluations).toBe(params[0].size);
  });

  it('produces deterministic results with the same seed', () => {
    const params = [{ size: 5 }, { size: 4 }, { size: 3 }];

    const rng1 = new Rng(77);
    const tc1 = construct(params, uniform, [], rng1);

    const rng2 = new Rng(77);
    const tc2 = construct(params, uniform, [], rng2);

    expect(tc1).not.toBeNull();
    expect(tc2).not.toBeNull();
    if (tc1 === null || tc2 === null) {
      return;
    }
    expect(tc1.values).toEqual(tc2.values);
  });

  it('produces different results with different seeds', () => {
    const params = [{ size: 10 }, { size: 10 }, { size: 10 }];

    const rng1 = new Rng(1);
    const tc1 = construct(params, uniform, [], rng1);

    const rng2 = new Rng(9999);
    const tc2 = construct(params, uniform, [], rng2);

    expect(tc1).not.toBeNull();
    expect(tc2).not.toBeNull();
    if (tc1 === null || tc2 === null) {
      return;
    }
    // With 10 values per param and equal scores, different seeds should produce
    // different test cases with very high probability.
    const same = tc1.values.every((v, i) => v === tc2.values[i]);
    expect(same).toBe(false);
  });

  it('reports the gain it accumulated while building the test case', () => {
    // The caller drops a full rescan of the finished case in favour of this
    // figure, so the two must agree. Each parameter contributes the score of the
    // value it settled on.
    const params = [{ size: 3 }, { size: 2 }, { size: 4 }];
    const perValue = [
      [5, 1, 1],
      [7, 2],
      [3, 3, 9, 3],
    ];
    const scoreValues: ScoreValuesFn = (_partial, pi, scores) => {
      for (let vi = 0; vi < scores.length; ++vi) {
        scores[vi] += perValue[pi][vi];
      }
    };

    const built = greedyConstruct(
      params,
      scoreValues,
      [],
      new Rng(11),
      createGreedyScratch(params),
    );
    expect(built).not.toBeNull();
    if (built === null) {
      return;
    }
    // Scores here do not depend on the partial assignment, so the greedy pick is
    // the highest-scoring value of each parameter.
    expect(built.testCase.values).toEqual([0, 0, 2]);
    expect(built.score).toBe(5 + 7 + 9);
  });

  it('reuses the caller scratch without growing it per construction', () => {
    // The scratch is the whole point of handing buffers in from outside: after
    // it has been sized once, no later construction may replace or grow any of
    // them, whatever the parameter or value counts.
    const params = Array.from({ length: 12 }, (_, pi) => ({ size: pi + 1 }));
    const scratch = createGreedyScratch(params);
    const orderRef = scratch.order;
    const scoresRef = scratch.scores;
    const bestValuesRef = scratch.bestValues;
    const maxValues = params[params.length - 1].size;

    const rng = new Rng(7);
    for (let i = 0; i < 25; ++i) {
      const built = greedyConstruct(params, uniform, [], rng, scratch);
      expect(built).not.toBeNull();
      expect(scratch.order).toBe(orderRef);
      expect(scratch.scores).toBe(scoresRef);
      expect(scratch.bestValues).toBe(bestValuesRef);
      expect(scratch.order.length).toBe(params.length);
      expect(scratch.scores.length).toBeLessThanOrEqual(maxValues);
      expect(scratch.bestValues.length).toBeLessThanOrEqual(maxValues);
    }
  });
});
