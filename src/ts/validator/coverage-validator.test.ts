import { describe, expect, it } from 'vitest';
import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode } from '../model/error.js';
import { MAX_PARAMETERS, Parameter } from '../model/parameter.js';
import type { GenerateResult, TestCase } from '../model/test-case.js';
import { createGenerateResult, UNASSIGNED } from '../model/test-case.js';
import {
  annotateClassCoverage,
  computeClassCoverage,
  validateCoverage,
} from './coverage-validator.js';

describe('validateCoverage', () => {
  const params2x2 = [
    new Parameter('os', ['win', 'mac']),
    new Parameter('browser', ['chrome', 'safari']),
  ];

  it('full pairwise coverage: 2x2 with all 4 combinations', () => {
    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome
      { values: [0, 1] }, // win, safari
      { values: [1, 0] }, // mac, chrome
      { values: [1, 1] }, // mac, safari
    ];

    const report = validateCoverage(params2x2, tests, 2);
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(4);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);
  });

  it('returns an error for contradictory constraints instead of full coverage', () => {
    const constraints = ['os=win', 'os!=win'].map((expression) => {
      const parsed = parseConstraint(expression, params2x2);
      expect(parsed.constraint).toBeDefined();
      return parsed.constraint;
    });
    const report = validateCoverage(
      params2x2,
      [],
      2,
      constraints.filter((constraint) => constraint !== undefined),
    );
    expect(report.error.code).toBe(ErrorCode.ConstraintError);
    expect(report.error.message).toBe('Constraints are unsatisfiable');
    expect(report.coverageRatio).not.toBe(1);
  });

  it('returns invalid input when a parameter has no valid values', () => {
    const report = validateCoverage(
      [new Parameter('A', ['0', '1'], [true, true]), new Parameter('B', ['0', '1'])],
      [],
      2,
    );
    expect(report.error.code).toBe(ErrorCode.InvalidInput);
    expect(report.coverageRatio).not.toBe(1);
  });

  it('partial coverage: 2x2 with only 2 of 4 combinations', () => {
    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome
      { values: [1, 1] }, // mac, safari
    ];

    const report = validateCoverage(params2x2, tests, 2);
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(2);
    expect(report.coverageRatio).toBe(0.5);
    expect(report.uncovered).toHaveLength(2);

    const uncoveredStrings = report.uncovered.map((u) => u.tuple.join(', '));
    expect(uncoveredStrings).toContain('os=win, browser=safari');
    expect(uncoveredStrings).toContain('os=mac, browser=chrome');
    for (const uncovered of report.uncovered) {
      expect(uncovered.indices).toHaveLength(2);
      expect(uncovered.indices?.every(([pi, vi]) => pi >= 0 && vi >= 0)).toBe(true);
    }
  });

  it('preserves exact indices when names and values contain equals signs', () => {
    const params = [new Parameter('A=B', ['x=y']), new Parameter('C', ['z'])];
    const report = validateCoverage(params, [], 2);
    expect(report.uncovered).toHaveLength(1);
    expect(report.uncovered[0].tuple).toEqual(['A=B=x=y', 'C=z']);
    expect(report.uncovered[0].indices).toEqual([
      [0, 0],
      [1, 0],
    ]);
  });

  it('3 params x 2 values pairwise: totalTuples = 12', () => {
    const params3x2 = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'safari']),
      new Parameter('arch', ['x64', 'arm']),
    ];

    // C(3,2) = 3 pairs, each pair has 2*2 = 4 tuples -> 12 total
    // Provide full coverage with enough test cases
    const tests: TestCase[] = [
      { values: [0, 0, 0] }, // win, chrome, x64
      { values: [0, 1, 1] }, // win, safari, arm
      { values: [1, 0, 1] }, // mac, chrome, arm
      { values: [1, 1, 0] }, // mac, safari, x64
    ];

    const report = validateCoverage(params3x2, tests, 2);
    expect(report.totalTuples).toBe(12);
    expect(report.coveredTuples).toBe(12);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);
  });

  it('strength 1: 1-wise coverage, totalTuples = sum of value counts', () => {
    const params = [
      new Parameter('os', ['win', 'mac', 'linux']),
      new Parameter('browser', ['chrome', 'safari']),
    ];
    // 1-wise: totalTuples = 3 + 2 = 5

    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome
      { values: [1, 1] }, // mac, safari
      { values: [2, 0] }, // linux, chrome
    ];

    const report = validateCoverage(params, tests, 1);
    expect(report.totalTuples).toBe(5);
    expect(report.coveredTuples).toBe(5);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('strength > params: rejected as invalid input', () => {
    const report = validateCoverage(params2x2, [], 5);
    expect(report.error.code).toBe(ErrorCode.InvalidInput);
  });

  it('strength 0: rejected as invalid input', () => {
    const report = validateCoverage(params2x2, [], 0);
    expect(report.error.code).toBe(ErrorCode.InvalidInput);
  });

  it('single parameter: strength=2 > 1 param, rejected as invalid input', () => {
    const params = [new Parameter('os', ['win', 'mac', 'linux'])];
    const report = validateCoverage(params, [], 2);
    expect(report.error.code).toBe(ErrorCode.InvalidInput);
  });

  it('empty test suite: coverageRatio=0, uncovered contains all tuples', () => {
    const report = validateCoverage(params2x2, [], 2);
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(0);
    expect(report.coverageRatio).toBe(0);
    expect(report.uncovered).toHaveLength(4);
  });

  it('uncovered tuple format: tuple strings and param names', () => {
    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome
    ];

    const report = validateCoverage(params2x2, tests, 2);
    // 3 uncovered out of 4
    expect(report.uncovered).toHaveLength(3);

    for (const u of report.uncovered) {
      // Each tuple entry should be "param=value" format
      for (const entry of u.tuple) {
        expect(entry).toMatch(/^[a-z]+=\w+$/);
      }
      // params should contain param names
      expect(u.params.length).toBe(2);
      expect(u.params).toContain('os');
      expect(u.params).toContain('browser');
      // reason should be present
      expect(u.reason).toBe('never covered');
    }
  });

  it('excludes constraint-invalid tuples from the universe', () => {
    // os = {win, mac}, browser = {chrome, ie}
    // Constraint: IF os=mac THEN browser!=ie
    //   removes (os=mac, browser=ie) from the universe -> 3 valid tuples.
    const params = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'ie']),
    ];
    const parse = parseConstraint('IF os=mac THEN browser!=ie', params);
    expect(parse.error.code).toBe(0);
    if (parse.constraint === null) {
      throw new Error(`constraint parsing failed: ${parse.error.message}`);
    }

    // Cover 2 of the 3 valid tuples; (win, ie) is missing.
    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome
      { values: [1, 0] }, // mac, chrome
    ];

    const report = validateCoverage(params, tests, 2, [parse.constraint]);
    expect(report.totalTuples).toBe(3);
    expect(report.coveredTuples).toBe(2);
    expect(report.uncovered).toHaveLength(1);
    expect(report.uncovered[0].tuple).toEqual(expect.arrayContaining(['os=win', 'browser=ie']));
    // The excluded tuple must NOT appear as uncovered.
    for (const u of report.uncovered) {
      expect(u.tuple).not.toEqual(expect.arrayContaining(['os=mac', 'browser=ie']));
    }
  });
});

describe('validateCoverage with invalid values', () => {
  it('excludes invalid-value tuples from the universe (oracle/generator agreement)', () => {
    // os = {win, mac, ie6(invalid)}, browser = {chrome, safari}.
    // Valid pairs: (win|mac) x (chrome|safari) = 4. Tuples involving os=ie6
    // (ie6/chrome, ie6/safari) must be excluded, matching the generator.
    const params = [
      new Parameter('os', ['win', 'mac', 'ie6'], [false, false, true]),
      new Parameter('browser', ['chrome', 'safari'], [false, false]),
    ];

    // Suite achieving full valid coverage (no invalid value referenced).
    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome
      { values: [0, 1] }, // win, safari
      { values: [1, 0] }, // mac, chrome
      { values: [1, 1] }, // mac, safari
    ];

    const report = validateCoverage(params, tests, 2);

    // Only the 4 valid pairs are in the universe; ie6-based tuples excluded.
    expect(report.totalTuples).toBe(4);
    expect(report.coveredTuples).toBe(4);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);

    // The invalid-value tuples must never surface as uncovered.
    for (const u of report.uncovered) {
      expect(u.tuple).not.toContain('os=ie6');
    }
  });
});

describe('validateCoverage with invalid value indices', () => {
  it('handles test case with out-of-range value index gracefully', () => {
    const params = [
      new Parameter('os', ['win', 'mac', 'linux']),
      new Parameter('browser', ['chrome', 'firefox', 'safari']),
    ];

    // Test case with invalid value index 999 for browser (only 3 values exist).
    const tests: TestCase[] = [
      { values: [0, 999] },
      { values: [0, 0] }, // valid: win, chrome
    ];

    // Should not crash.
    const report = validateCoverage(params, tests, 2);

    // The row carrying value 999 is rejected whole and reported in
    // invalidTests, so its in-range os value does not count either. Only the
    // accepted row (win, chrome) covers a tuple.
    expect(report.totalTuples).toBe(9); // C(2,2) * 3 * 3 = 9
    expect(report.coveredTuples).toBe(1);
    expect(report.coverageRatio).toBeCloseTo(1 / 9);
    expect(report.uncovered).toHaveLength(8);
    expect(report.invalidTests).toEqual([
      { testIndex: 0, reason: 'value index 999 is out of range for parameter browser' },
    ]);
  });

  it('rejects a parameter count beyond the documented limit', () => {
    // Feasibility search walks one parameter per level, so an oversized model
    // has to be turned away as invalid input before any search starts.
    const params = Array.from(
      { length: 200_000 },
      (_, index) => new Parameter(`P${index}`, ['a', 'b']),
    );
    const parse = parseConstraint('P0="a" OR P1="b"', params);
    expect(parse.constraint).toBeDefined();

    // biome-ignore lint/style/noNonNullAssertion: guarded by expect above.
    const report = validateCoverage(params, [], 1, [parse.constraint!]);

    expect(report.error.code).toBe(ErrorCode.InvalidInput);
    expect(report.totalTuples).toBe(0);
  });

  it('accepts the largest documented parameter count', () => {
    const params = Array.from(
      { length: MAX_PARAMETERS },
      (_, index) => new Parameter(`P${index}`, ['a', 'b']),
    );
    const parse = parseConstraint('P0="a" OR P1="b"', params);
    expect(parse.constraint).toBeDefined();

    // biome-ignore lint/style/noNonNullAssertion: guarded by expect above.
    const report = validateCoverage(params, [], 1, [parse.constraint!]);

    expect(report.error.code).toBe(ErrorCode.Ok);
    expect(report.totalTuples).toBe(2 * MAX_PARAMETERS);
  });

  it('rejects one parameter past the limit', () => {
    const params = Array.from(
      { length: MAX_PARAMETERS + 1 },
      (_, index) => new Parameter(`P${index}`, ['a', 'b']),
    );

    const report = validateCoverage(params, [], 1);

    expect(report.error.code).toBe(ErrorCode.InvalidInput);
    expect(report.error.message).toBe('Parameter count 1025 exceeds maximum of 1024');
  });
});

describe('computeClassCoverage', () => {
  it('params with equivalence classes: verify class tuple coverage', () => {
    // os: win and linux are "desktop", mac is "apple"
    // browser: chrome and firefox are "standard", safari is "webkit"
    const os = new Parameter('os', ['win', 'mac', 'linux']);
    os.setEquivalenceClasses(['desktop', 'apple', 'desktop']);

    const browser = new Parameter('browser', ['chrome', 'firefox', 'safari']);
    browser.setEquivalenceClasses(['standard', 'standard', 'webkit']);

    const params = [os, browser];

    // Class tuples for pairwise: {desktop, apple} x {standard, webkit} = 4 tuples
    const tests: TestCase[] = [
      { values: [0, 0] }, // win(desktop), chrome(standard)
      { values: [1, 2] }, // mac(apple), safari(webkit)
      { values: [2, 2] }, // linux(desktop), safari(webkit)
      { values: [1, 0] }, // mac(apple), chrome(standard)
    ];

    const report = computeClassCoverage(params, tests, 2);
    expect(report.totalClassTuples).toBe(4);
    expect(report.coveredClassTuples).toBe(4);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('partial class coverage', () => {
    const os = new Parameter('os', ['win', 'mac']);
    os.setEquivalenceClasses(['desktop', 'apple']);

    const browser = new Parameter('browser', ['chrome', 'safari']);
    browser.setEquivalenceClasses(['standard', 'webkit']);

    const params = [os, browser];

    // Only cover 1 of 4 class tuples
    const tests: TestCase[] = [{ values: [0, 0] }]; // desktop, standard

    const report = computeClassCoverage(params, tests, 2);
    expect(report.totalClassTuples).toBe(4);
    expect(report.coveredClassTuples).toBe(1);
    expect(report.coverageRatio).toBe(0.25);
  });

  it('constraint excludes unsatisfiable class tuple, suite reports 1.0', () => {
    // os = {win(desktop), mac(apple)}, browser = {chrome(modern), ie(legacy)}.
    // Constraint: IF os=mac THEN browser!=ie. The class tuple (apple, legacy)
    // has only the (mac, ie) representative, which the constraint forbids, so it
    // is excluded. A suite covering the three valid class tuples reports 1.0.
    const os = new Parameter('os', ['win', 'mac']);
    os.setEquivalenceClasses(['desktop', 'apple']);
    const browser = new Parameter('browser', ['chrome', 'ie']);
    browser.setEquivalenceClasses(['modern', 'legacy']);
    const params = [os, browser];

    const parse = parseConstraint('IF os=mac THEN browser!=ie', params);
    expect(parse.error.code).toBe(0);
    expect(parse.constraint).toBeDefined();

    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome -> desktop, modern
      { values: [0, 1] }, // win, ie     -> desktop, legacy
      { values: [1, 0] }, // mac, chrome -> apple, modern
    ];

    // biome-ignore lint/style/noNonNullAssertion: guarded by expect above.
    const report = computeClassCoverage(params, tests, 2, [parse.constraint!]);
    expect(report.totalClassTuples).toBe(3);
    expect(report.coveredClassTuples).toBe(3);
    expect(report.coverageRatio).toBe(1.0);

    // Without constraints the same suite is incomplete (4 tuples, 3 covered).
    const unconstrained = computeClassCoverage(params, tests, 2);
    expect(unconstrained.totalClassTuples).toBe(4);
    expect(unconstrained.coveredClassTuples).toBe(3);
    expect(unconstrained.coverageRatio).toBeLessThan(1.0);
  });

  it('invalid value excludes unsatisfiable class tuple, suite reports 1.0', () => {
    // os = {win(desktop), mac(apple), ie6(legacy, invalid)}.
    // The only "legacy" value is invalid, so class tuples requiring os=legacy
    // have no valid representative and are excluded.
    const os = new Parameter('os', ['win', 'mac', 'ie6'], [false, false, true]);
    os.setEquivalenceClasses(['desktop', 'apple', 'legacy']);
    const browser = new Parameter('browser', ['chrome', 'safari']);
    browser.setEquivalenceClasses(['modern', 'webkit']);
    const params = [os, browser];

    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome -> desktop, modern
      { values: [0, 1] }, // win, safari -> desktop, webkit
      { values: [1, 0] }, // mac, chrome -> apple, modern
      { values: [1, 1] }, // mac, safari -> apple, webkit
    ];

    const report = computeClassCoverage(params, tests, 2);
    expect(report.totalClassTuples).toBe(4);
    expect(report.coveredClassTuples).toBe(4);
    expect(report.coverageRatio).toBe(1.0);
  });

  it('no equivalence classes: empty universe is vacuously complete', () => {
    const params = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'safari']),
    ];

    const tests: TestCase[] = [{ values: [0, 0] }];
    const report = computeClassCoverage(params, tests, 2);
    // No class tuples to cover -> vacuously complete (1.0), matching C++. A
    // ratio of 0 is reserved for the error exits, so an empty universe is told
    // apart from a failure by the error, never by the ratio.
    expect(report.totalClassTuples).toBe(0);
    expect(report.coveredClassTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.error.code).toBe(ErrorCode.Ok);
  });

  it('strength 0: empty universe is vacuously complete', () => {
    const os = new Parameter('os', ['win', 'mac']);
    os.setEquivalenceClasses(['desktop', 'apple']);
    const report = computeClassCoverage([os], [], 0);
    expect(report.totalClassTuples).toBe(0);
    expect(report.coveredClassTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.error.code).toBe(ErrorCode.Ok);
  });

  it('strength above the parameter count: empty universe is vacuously complete', () => {
    const os = new Parameter('os', ['win', 'mac']);
    os.setEquivalenceClasses(['desktop', 'apple']);
    const report = computeClassCoverage([os], [{ values: [0] }], 2);
    expect(report.totalClassTuples).toBe(0);
    expect(report.coveredClassTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.error.code).toBe(ErrorCode.Ok);
  });
});

describe('annotateClassCoverage', () => {
  it('annotates result with classCoverage when classes are defined', () => {
    const os = new Parameter('os', ['win', 'mac']);
    os.setEquivalenceClasses(['desktop', 'apple']);

    const browser = new Parameter('browser', ['chrome', 'safari']);
    browser.setEquivalenceClasses(['standard', 'webkit']);

    const params = [os, browser];

    const result: GenerateResult = createGenerateResult();
    result.tests = [{ values: [0, 0] }, { values: [0, 1] }, { values: [1, 0] }, { values: [1, 1] }];

    annotateClassCoverage(result, params, 2);

    expect(result.classCoverage).toBeDefined();
    expect(result.classCoverage?.totalClassTuples).toBe(4);
    expect(result.classCoverage?.coveredClassTuples).toBe(4);
    expect(result.classCoverage?.classCoverageRatio).toBe(1.0);
  });

  it('does not annotate when no equivalence classes exist', () => {
    const params = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'safari']),
    ];

    const result: GenerateResult = createGenerateResult();
    result.tests = [{ values: [0, 0] }];

    annotateClassCoverage(result, params, 2);

    expect(result.classCoverage).toBeUndefined();
  });
});

class LateClassWitnessConstraint implements ConstraintNode {
  evaluate(assignment: number[]): ConstraintResult {
    if (assignment.some((value) => value === UNASSIGNED)) {
      return ConstraintResult.Unknown;
    }
    if (assignment[assignment.length - 1] === 0) {
      return ConstraintResult.True;
    }
    return assignment.slice(0, -1).every((value) => value === 1)
      ? ConstraintResult.True
      : ConstraintResult.False;
  }

  toString(): string {
    return 'late-class-witness';
  }
}

describe('class coverage search budget', () => {
  it('propagates budget exhaustion instead of reporting false full coverage', () => {
    const params = Array.from({ length: 22 }, (_, index) => {
      const parameter = new Parameter(`P${index}`, ['0', '1']);
      if (index === 21) {
        parameter.setEquivalenceClasses(['', 'hard']);
      }
      return parameter;
    });
    const result = createGenerateResult();

    annotateClassCoverage(result, params, 1, [new LateClassWitnessConstraint()]);

    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.error.message).toBe('Constraint search budget exceeded');
    expect(result.classCoverage).toBeUndefined();
  });
});

/**
 * Expression whose only cheap witnesses are gate="open" and pick="cheap". With
 * both fixed the other way it stays undecided until "relief" is assigned, and
 * "none" — its only satisfying value — is invalid, so proving the branch
 * unsatisfiable costs more than the search budget allows. Every filler
 * parameter has more valid values than "relief", so a search ordering
 * parameters by ascending domain size settles the branch immediately while one
 * walking parameters in declaration order does not.
 */
const COSTLY_REPRESENTATIVE_EXPRESSION = 'gate="open" OR pick="cheap" OR relief="none"';

/**
 * Build a model whose "same" class holds one cheap and one costly
 * representative. `cheapFirst` places the cheap representative at value index 0
 * when true and at value index 1 when false; the class tuple is feasible either
 * way, so both orders must produce the same verdict.
 */
function representativeOrderParameters(cheapFirst: boolean) {
  return [
    {
      name: 'gate',
      values: ['open', 'shut'],
      equivalenceClasses: ['open_class', 'shut_class'],
    },
    {
      name: 'pick',
      values: cheapFirst ? ['cheap', 'costly'] : ['costly', 'cheap'],
      equivalenceClasses: ['same', 'same'],
    },
    ...Array.from({ length: 14 }, (_, index) => ({
      name: `f${index}`,
      values: ['a', 'b', 'c'],
    })),
    { name: 'relief', values: ['r0', 'r1', 'none'], invalid: [false, false, true] },
  ];
}

function representativeOrderModel(cheapFirst: boolean): Parameter[] {
  return representativeOrderParameters(cheapFirst).map((spec) => {
    const parameter = spec.invalid
      ? new Parameter(spec.name, spec.values, spec.invalid)
      : new Parameter(spec.name, spec.values);
    if (spec.equivalenceClasses) {
      parameter.setEquivalenceClasses(spec.equivalenceClasses);
    }
    return parameter;
  });
}

describe('class tuple representatives', () => {
  it('reaches the same verdict whichever representative is cheap', () => {
    const totals = [true, false].map((cheapFirst) => {
      const params = representativeOrderModel(cheapFirst);
      const parse = parseConstraint(COSTLY_REPRESENTATIVE_EXPRESSION, params);
      expect(parse.constraint).toBeDefined();
      const result = createGenerateResult();

      // biome-ignore lint/style/noNonNullAssertion: guarded by expect above.
      annotateClassCoverage(result, params, 2, [parse.constraint!]);

      // A representative whose search runs out of budget must not decide the
      // tuple: the "same" class also holds a trivially satisfiable
      // representative, which makes the tuple feasible from either value order.
      expect(result.error.code).toBe(ErrorCode.Ok);
      expect(result.classCoverage).toBeDefined();
      return result.classCoverage?.totalClassTuples;
    });

    expect(totals[0]).toBe(totals[1]);
    expect(totals[0]).toBe(2);
  });
});

describe('class coverage scaling', () => {
  it('projects a one-million-tuple class universe without per-tuple value scans', () => {
    const classCount = 1000;
    const left = new Parameter(
      'left',
      Array.from({ length: classCount }, (_, index) => `l${index}`),
    );
    const right = new Parameter(
      'right',
      Array.from({ length: classCount }, (_, index) => `r${index}`),
    );
    left.setEquivalenceClasses(Array.from({ length: classCount }, (_, index) => `lc${index}`));
    right.setEquivalenceClasses(Array.from({ length: classCount }, (_, index) => `rc${index}`));

    const report = computeClassCoverage([left, right], [{ values: [0, 0] }], 2);

    expect(report.error.code).toBe(ErrorCode.Ok);
    expect(report.totalClassTuples).toBe(1_000_000);
    expect(report.coveredClassTuples).toBe(1);
  });

  it('preflights the class universe rather than the raw value universe', () => {
    const valueCount = 4001;
    const left = new Parameter(
      'left',
      Array.from({ length: valueCount }, (_, index) => `l${index}`),
    );
    const right = new Parameter(
      'right',
      Array.from({ length: valueCount }, (_, index) => `r${index}`),
    );
    left.setEquivalenceClasses(new Array<string>(valueCount).fill('one'));
    right.setEquivalenceClasses(new Array<string>(valueCount).fill('one'));

    const report = computeClassCoverage([left, right], [{ values: [0, 0] }], 2);

    expect(report.error.code).toBe(ErrorCode.Ok);
    expect(report.totalClassTuples).toBe(1);
    expect(report.coveredClassTuples).toBe(1);
    expect(report.coverageRatio).toBe(1);
  });
});

describe('constraint feasibility and invalid test filtering', () => {
  it('excludes a tuple that has no complete constraint witness', () => {
    const params = [
      new Parameter('A', ['0', '1']),
      new Parameter('B', ['0', '1']),
      new Parameter('C', ['0', '1']),
    ];
    const constraints = ['IF A=0 THEN C=0', 'IF B=0 THEN C=1'].map((expression) => {
      const parsed = parseConstraint(expression, params);
      expect(parsed.constraint).toBeDefined();
      return parsed.constraint;
    });
    const report = validateCoverage(
      params,
      [{ values: [0, 1, 0] }, { values: [1, 0, 1] }, { values: [1, 1, 0] }],
      2,
      constraints.filter((constraint) => constraint !== undefined),
    );
    expect(report.totalTuples).toBe(9);
    expect(
      report.uncovered.some((tuple) => tuple.tuple.includes('A=0') && tuple.tuple.includes('B=0')),
    ).toBe(false);
  });

  it('does not count missing-column or constraint-violating rows', () => {
    const params = [
      new Parameter('A', ['0', '1']),
      new Parameter('B', ['0', '1']),
      new Parameter('C', ['0', '1']),
    ];
    const parsed = parseConstraint('IF A=0 THEN C=0', params);
    expect(parsed.constraint).toBeDefined();
    const report = validateCoverage(
      params,
      [{ values: [0, 0] }, { values: [0, 0, 1] }],
      2,
      parsed.constraint ? [parsed.constraint] : [],
    );
    expect(
      report.uncovered.some((tuple) => tuple.tuple.includes('A=0') && tuple.tuple.includes('B=0')),
    ).toBe(true);
  });
});

describe('resource budgets', () => {
  it('rejects a 2^64 tuple product instead of wrapping to zero', () => {
    const params = Array.from(
      { length: 8 },
      (_, pi) =>
        new Parameter(
          `P${pi}`,
          Array.from({ length: 256 }, (_, vi) => `${vi}`),
        ),
    );
    const report = validateCoverage(params, [], 8);
    expect(report.error.code).toBe(4);
    expect(report.totalTuples).toBe(0);
    expect(report.coverageRatio).not.toBe(1);
  });

  it('caps uncovered diagnostics while retaining exact counts', () => {
    const report = validateCoverage(
      [
        new Parameter(
          'A',
          Array.from({ length: 40 }, (_, i) => `${i}`),
        ),
        new Parameter(
          'B',
          Array.from({ length: 40 }, (_, i) => `${i}`),
        ),
      ],
      [],
      2,
    );
    expect(report.uncoveredCount).toBe(1600);
    expect(report.uncovered).toHaveLength(1000);
    expect(report.omittedUncovered).toBe(600);
  });

  it('rejects combination metadata before materialization', () => {
    const params = Array.from({ length: 200 }, (_, i) => new Parameter(`P${i}`, ['only']));
    expect(validateCoverage(params, [], 3).error.code).toBe(4);
  });
});

describe('constrained validation on a covering suite', () => {
  /**
   * Strength-2 covering array for binary parameters. The all-zero and all-one
   * rows cover the (0,0) and (1,1) pairs; for any two distinct parameters some
   * bit of their indices differs, so the row carrying that bit and its
   * complement cover (0,1) and (1,0).
   */
  function binaryCoveringSuite(count: number): TestCase[] {
    const tests: TestCase[] = [
      { values: new Array<number>(count).fill(0) },
      { values: new Array<number>(count).fill(1) },
    ];
    for (let bit = 0; 1 << bit < count; ++bit) {
      const row: number[] = new Array<number>(count);
      const complement: number[] = new Array<number>(count);
      for (let i = 0; i < count; ++i) {
        const value = (i >> bit) & 1;
        row[i] = value;
        complement[i] = 1 - value;
      }
      tests.push({ values: row });
      tests.push({ values: complement });
    }
    return tests;
  }

  function fastestMs(run: () => void, repetitions: number): number {
    let best = Number.POSITIVE_INFINITY;
    for (let i = 0; i < repetitions; ++i) {
      const start = performance.now();
      run();
      best = Math.min(best, performance.now() - start);
    }
    return best;
  }

  const parameterCount = 200;
  const params = Array.from(
    { length: parameterCount },
    (_, i) => new Parameter(`p${i}`, ['v0', 'v1']),
  );
  const tests = binaryCoveringSuite(parameterCount);
  const trivial = parseConstraint('p0 = v0 OR p0 = v1', params);

  it('leaves the scratch assignment untouched after a successful search', () => {
    // The tuple loop hands its own assignment buffer to the feasibility search
    // instead of copying it per tuple, so a search that finds a witness has to
    // undo its own writes. Leaking them would make later tuples be judged
    // against the previous witness rather than against themselves.
    //
    // A=0,B=0 is feasible and its first witness assigns C=0. The very next
    // tuple, A=0,B=1, is feasible only with C=1 — so a leaked C=0 turns it
    // infeasible and drops it out of the universe.
    const abc = [
      new Parameter('A', ['0', '1']),
      new Parameter('B', ['0', '1']),
      new Parameter('C', ['0', '1']),
    ];
    const parsed = parseConstraint('IF B=1 THEN C=1', abc);
    expect(parsed.error.code).toBe(ErrorCode.Ok);
    const constraints = parsed.constraint === null ? [] : [parsed.constraint];

    const report = validateCoverage(abc, [], 2, constraints);

    expect(report.error.code).toBe(ErrorCode.Ok);
    // (A,B) and (A,C) contribute 4 tuples each; (B,C) loses only (B=1, C=0).
    expect(report.totalTuples).toBe(11);
    expect(report.uncoveredCount).toBe(11);
  });

  it('a tuple a valid test covers needs no feasibility search', () => {
    // The constraint holds for every assignment of p0, so it excludes nothing
    // and the two reports have to agree field for field.
    expect(trivial.error.code).toBe(ErrorCode.Ok);
    const constraints = trivial.constraint === null ? [] : [trivial.constraint];

    const unconstrained = validateCoverage(params, tests, 2);
    const constrained = validateCoverage(params, tests, 2, constraints);

    expect(constrained.totalTuples).toBe(unconstrained.totalTuples);
    expect(constrained.coveredTuples).toBe(unconstrained.coveredTuples);
    expect(constrained.uncoveredCount).toBe(unconstrained.uncoveredCount);
    expect(constrained.omittedUncovered).toBe(unconstrained.omittedUncovered);
    expect(constrained.uncovered).toHaveLength(unconstrained.uncovered.length);
    expect(constrained.coverageRatio).toBe(unconstrained.coverageRatio);
    expect(constrained.invalidTests).toHaveLength(unconstrained.invalidTests.length);
    expect(constrained.error.code).toBe(ErrorCode.Ok);

    // The suite covers the whole universe, which is what makes the search
    // redundant in the first place.
    const expectedTuples = ((parameterCount * (parameterCount - 1)) / 2) * 4;
    expect(constrained.totalTuples).toBe(expectedTuples);
    expect(constrained.coveredTuples).toBe(expectedTuples);
    expect(constrained.coverageRatio).toBe(1.0);
  });

  it('runs within twice the unconstrained time', () => {
    const constraints = trivial.constraint === null ? [] : [trivial.constraint];
    const unconstrainedMs = fastestMs(() => validateCoverage(params, tests, 2), 3);
    const constrainedMs = fastestMs(() => validateCoverage(params, tests, 2, constraints), 3);

    expect(constrainedMs).toBeLessThan(2 * unconstrainedMs);
  });
});

describe('class projection cost', () => {
  /** Parameters whose class names are padded to `nameLength` characters. */
  function paddedClassParams(
    parameterCount: number,
    values: number,
    classes: number,
    nameLength: number,
  ): Parameter[] {
    return Array.from({ length: parameterCount }, (_, index) => {
      const parameter = new Parameter(
        `p${index}`,
        Array.from({ length: values }, (_, value) => `v${value}`),
      );
      parameter.setEquivalenceClasses(
        Array.from({ length: values }, (_, value) => `c${value % classes}`.padEnd(nameLength, 'x')),
      );
      return parameter;
    });
  }

  function spreadSuite(parameterCount: number, values: number, count: number): TestCase[] {
    return Array.from({ length: count }, (_, t) => ({
      values: Array.from({ length: parameterCount }, (_, index) => (t * 7 + index * 5) % values),
    }));
  }

  it('does not depend on how long the class names are', () => {
    // Resolving a value to its class runs once per (combination, test,
    // position), so it has to be a flat array read. A name-keyed lookup would
    // instead make the projection scale with how long the class labels happen
    // to be — a property of the model's vocabulary, not of its size.
    const parameterCount = 24;
    const values = 12;
    const classes = 4;
    const shortNames = paddedClassParams(parameterCount, values, classes, 2);
    const longNames = paddedClassParams(parameterCount, values, classes, 256);
    const tests = spreadSuite(parameterCount, values, 400);

    // Padding renames the classes without changing the class structure, so both
    // models must describe the same universe and the same coverage.
    const shortReport = computeClassCoverage(shortNames, tests, 2);
    const longReport = computeClassCoverage(longNames, tests, 2);
    expect(shortReport.error.code).toBe(ErrorCode.Ok);
    expect(longReport.totalClassTuples).toBe(shortReport.totalClassTuples);
    expect(longReport.coveredClassTuples).toBe(shortReport.coveredClassTuples);
    expect(longReport.coverageRatio).toBe(shortReport.coverageRatio);

    let shortMs = Number.POSITIVE_INFINITY;
    let longMs = Number.POSITIVE_INFINITY;
    for (let i = 0; i < 5; ++i) {
      let start = performance.now();
      computeClassCoverage(shortNames, tests, 2);
      shortMs = Math.min(shortMs, performance.now() - start);
      start = performance.now();
      computeClassCoverage(longNames, tests, 2);
      longMs = Math.min(longMs, performance.now() - start);
    }

    expect(longMs).toBeLessThan(1.5 * shortMs);
  });
});

// ---------------------------------------------------------------------------
// invalidTests
//
// A row the validator refuses to count is the one thing a caller cannot see
// from the coverage numbers alone: a rejected row lowers coverage exactly like
// a missing one. Every rejection category therefore has to name the row and say
// why, and the five categories are checked in a fixed order — arity first, then
// per-parameter unassigned / out of range / marked invalid, then the
// constraints. The reasons are the same strings the C++ validator produces.
// ---------------------------------------------------------------------------

describe('validateCoverage invalidTests', () => {
  const params = [
    new Parameter('A', ['a0', 'a1']),
    new Parameter('B', ['b0', 'bad'], [false, true]),
    new Parameter('C', ['c0', 'c1']),
  ];

  function constraint(expression: string): ConstraintNode[] {
    const parsed = parseConstraint(expression, params);
    expect(parsed.constraint).toBeDefined();
    return parsed.constraint ? [parsed.constraint] : [];
  }

  it('names every rejected row and its reason', () => {
    const tests: TestCase[] = [
      { values: [0, 0] }, // 0: too few values
      { values: [0, UNASSIGNED, 0] }, // 1: B left unassigned
      { values: [0, 99, 0] }, // 2: B out of range
      { values: [0, 1, 0] }, // 3: B=bad is marked invalid
      { values: [0, 0, 1] }, // 4: violates the constraint
      { values: [0, 0, 0] }, // 5: accepted
      { values: [0, 0, 0, 0] }, // 6: too many values
    ];

    const report = validateCoverage(params, tests, 2, constraint('IF A=a0 THEN C=c0'));
    expect(report.error.code).toBe(ErrorCode.Ok);

    expect(report.invalidTests).toEqual([
      { testIndex: 0, reason: 'expected 3 value(s), got 2' },
      { testIndex: 1, reason: 'missing value for parameter B' },
      { testIndex: 2, reason: 'value index 99 is out of range for parameter B' },
      { testIndex: 3, reason: 'value B=bad is marked invalid' },
      {
        testIndex: 4,
        reason: 'violates constraint #1 (constraint evaluation is false or indeterminate)',
      },
      { testIndex: 6, reason: 'expected 3 value(s), got 4' },
    ]);

    // Only row 5 survived, so it is the only one that can contribute coverage.
    expect(report.coveredTuples).toBe(3);
  });

  it('is empty when every row is accepted', () => {
    const accepted = [new Parameter('A', ['a0', 'a1']), new Parameter('B', ['b0', 'b1'])];
    const report = validateCoverage(accepted, [{ values: [0, 0] }, { values: [1, 1] }], 2);

    expect(report.error.code).toBe(ErrorCode.Ok);
    expect(report.invalidTests).toEqual([]);
  });

  it('reports a non-integer value index as out of range rather than reading past the domain', () => {
    const report = validateCoverage(params, [{ values: [0, 0.5, 0] }], 2);

    expect(report.error.code).toBe(ErrorCode.Ok);
    expect(report.invalidTests).toEqual([
      { testIndex: 0, reason: 'value index 0.5 is out of range for parameter B' },
    ]);
  });
});
