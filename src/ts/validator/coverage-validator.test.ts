import { describe, expect, it } from 'vitest';
import {
  COSTLY_REPRESENTATIVE_EXPRESSION,
  representativeOrderParameters,
} from '../../../tests/util/class-tuple-fixture.js';
import { fastestEach } from '../../../tests/util/timing.js';
import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode } from '../model/error.js';
import { MAX_STRING_BYTES } from '../model/limits.js';
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

  it('warns through the single error mapping', () => {
    // validateParameters names the offending parameter and carries no detail,
    // so a warning assembled as "message: detail" would end in a dangling
    // separator.
    const os = new Parameter('os', ['win', 'mac'], [true, true]);
    os.setEquivalenceClasses(['desktop', 'apple']);
    const result: GenerateResult = createGenerateResult();

    annotateClassCoverage(result, [os, new Parameter('browser', ['chrome'])], 2);

    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.detail).toBe('');
    expect(result.warnings).toEqual([result.error.message]);
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

/**
 * Rejects any assignment pinning both parameters inside their large class, and
 * counts how often it is asked.
 *
 * The escape value each parameter also carries keeps the model satisfiable, so
 * the class tuple over the two large classes is the only infeasible one — and
 * its representatives are the whole cross product of those classes.
 */
class WideClassRejectConstraint implements ConstraintNode {
  evaluations = 0;
  private readonly escapeIndex: number;

  constructor(escapeIndex: number) {
    this.escapeIndex = escapeIndex;
  }

  evaluate(assignment: number[]): ConstraintResult {
    ++this.evaluations;
    if (assignment[0] === UNASSIGNED || assignment[1] === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    return assignment[0] < this.escapeIndex && assignment[1] < this.escapeIndex
      ? ConstraintResult.False
      : ConstraintResult.True;
  }

  toString(): string {
    return 'wide-class-reject';
  }
}

/** Parameters named P0.. over a shared binary domain. */
function binaryParams(count: number): Parameter[] {
  return Array.from({ length: count }, (_, index) => new Parameter(`P${index}`, ['0', '1']));
}

describe('class coverage search budget', () => {
  it('propagates budget exhaustion instead of reporting false full coverage', () => {
    const params = binaryParams(22);
    params[21].setEquivalenceClasses(['', 'hard']);
    const result = createGenerateResult();

    annotateClassCoverage(result, params, 1, [new LateClassWitnessConstraint()]);

    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.error.message).toBe('Constraint search budget exceeded');
    expect(result.classCoverage).toBeUndefined();
  });

  it('does not search a class tuple that a valid test already witnesses', () => {
    // The model above, plus a valid row covering the "hard" class. That row is a
    // complete assignment of valid values satisfying every constraint, and its
    // value in this parameter is a representative of the class tuple, so the
    // feasibility search has nothing left to establish. Running it anyway spends
    // the whole node budget and reports a covered tuple as undecidable.
    const params = binaryParams(22);
    params[21].setEquivalenceClasses(['', 'hard']);
    const result = createGenerateResult();
    // Every parameter at its second value satisfies the constraint, and it is
    // the "hard" class that the last parameter then takes.
    result.tests = [{ values: new Array<number>(22).fill(1) }];

    annotateClassCoverage(result, params, 1, [new LateClassWitnessConstraint()]);

    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.classCoverage?.totalClassTuples).toBe(1);
    expect(result.classCoverage?.coveredClassTuples).toBe(1);
    expect(result.classCoverage?.classCoverageRatio).toBe(1);
  });

  it('stops representative enumeration on a budget shared across representatives', () => {
    // Both parameters carry one large class and one escape value. The model is
    // satisfiable through the escape values, so validation reaches the class
    // tuple over the two large classes — and that tuple's representatives are
    // the whole cross product of the two classes, every one of them rejected.
    // Without one budget shared across them, each cheap rejection starts a fresh
    // budget that never runs out and the enumeration walks all of them.
    const classValues = 4000;
    const representatives = classValues * classValues;
    const params = ['left', 'right'].map((name) => {
      const values = Array.from({ length: classValues }, (_, index) => `${name}${index}`);
      const classes = new Array<string>(classValues).fill('many');
      values.push(`${name}_escape`);
      classes.push('escape_class');
      const parameter = new Parameter(name, values);
      parameter.setEquivalenceClasses(classes);
      return parameter;
    });
    const constraint = new WideClassRejectConstraint(classValues);
    const result = createGenerateResult();

    annotateClassCoverage(result, params, 2, [constraint]);

    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.error.message).toBe('Constraint search budget exceeded');
    expect(result.classCoverage).toBeUndefined();

    // Every representative costs at least one search node, so a bounded total of
    // nodes is a bounded number of representatives: the enumeration has to stop
    // short of the cross product rather than sweep it.
    expect(constraint.evaluations).toBeLessThan(representatives);
  }, 60_000);
});

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
      expect(
        result.error.code,
        'a budget-exceeded verdict on this model means one class tuple no longer affords more ' +
          'than a single full search, so the costly representative consumed the whole of ' +
          'MAX_CLASS_TUPLE_SEARCH_NODES before the feasible one was reached',
      ).toBe(ErrorCode.Ok);
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

/**
 * Undecided until every parameter is assigned, and rejecting at the leaf, so
 * concluding anything costs a sweep of the whole space.
 */
class LateRejectConstraint implements ConstraintNode {
  evaluate(assignment: number[]): ConstraintResult {
    for (const value of assignment) {
      if (value === UNASSIGNED) {
        return ConstraintResult.Unknown;
      }
    }
    return ConstraintResult.False;
  }

  toString(): string {
    return 'late-reject';
  }
}

/**
 * Satisfied as soon as the first parameter takes its first value, and a
 * LateRejectConstraint otherwise. The whole-model search tries first values
 * first and so settles at once, while a tuple pinning that parameter to its
 * second value pays the sweep.
 */
class GatedLateRejectConstraint implements ConstraintNode {
  evaluate(assignment: number[]): ConstraintResult {
    if (assignment[0] === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (assignment[0] === 0) {
      return ConstraintResult.True;
    }
    for (const value of assignment) {
      if (value === UNASSIGNED) {
        return ConstraintResult.Unknown;
      }
    }
    return ConstraintResult.False;
  }

  toString(): string {
    return 'gated-late-reject';
  }
}

// The feasibility search is bounded so a hard model terminates, which leaves two
// answers that look alike from the outside and must not: a search that finished
// and found nothing, and one that ran out of nodes. Both exits report the budget
// explicitly, and an undecided tuple is counted in no bucket at all.
describe('feasibility search budget', () => {
  it('reports an exhausted model search as budget exceeded, not unsatisfiable', () => {
    // The constraint can only be decided at a leaf, and 22 binary parameters put
    // more leaves below the root than the node budget covers. "Constraints are
    // unsatisfiable" would claim a proof the search never finished.
    const report = validateCoverage(binaryParams(22), [], 2, [new LateRejectConstraint()]);

    expect(report.error.code).toBe(ErrorCode.ConstraintError);
    expect(report.error.message).toBe('Constraint search budget exceeded');
    expect(report.error.detail).toBe(
      'The constraint model is too complex to solve within the search budget',
    );
    expect(report.totalTuples).toBe(0);
  }, 30_000);

  it('stops on an exhausted tuple search without counting that tuple', () => {
    // The model is satisfiable at the first assignment tried, so validation
    // reaches the tuple loop; the tuples pinning P0 to its second value then
    // cost a sweep of the remaining 22 parameters, which the node budget does
    // not cover.
    const report = validateCoverage(binaryParams(24), [], 2, [new GatedLateRejectConstraint()]);

    expect(report.error.code).toBe(ErrorCode.ConstraintError);
    expect(report.error.message).toBe('Constraint search budget exceeded');
    expect(report.error.detail).toBe(
      'Tuple feasibility could not be determined within the search budget',
    );

    // Validation stops on the undecided tuple. The two (P0=0, P1=*) tuples ahead
    // of it are counted, and the undecided one is counted as neither covered,
    // uncovered nor excluded.
    expect(report.totalTuples).toBe(2);
    expect(report.coveredTuples).toBe(0);
    expect(report.uncoveredCount).toBe(2);
  }, 30_000);
});

/// Ceiling on a hang, not a performance budget: these gates measure suites big
/// enough that a default unit-test timeout does not apply, and they run under
/// coverage instrumentation on a shared runner. The assertions compare two
/// measurements from the same run, so they are unaffected by it.
const MEASUREMENT_TIMEOUT_MS = 120_000;

/// Rounds each timing gate below samples. Every gate here is an upper bound, so
/// the high side of the ratio is the one that matters, and the fastest-of-N
/// floor converges from above as N grows: past ten the high side stops moving.
/// The value is set by whichever gate has the least room between its honest
/// ratio and the regression it separates, since a gate with room tolerates a
/// looser estimate. It matches the rounds the C++ tier samples for the same
/// property, so neither port reads a steadier estimate than the other.
const TIMING_RUNS = 15;

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

  it('skips the feasibility search for a tuple a valid test witnesses', {
    timeout: MEASUREMENT_TIMEOUT_MS,
  }, () => {
    // A model large enough that a run lands in the hundreds of milliseconds. At
    // tens of milliseconds the ratio below measures the machine as much as the
    // validator. The smaller model above is enough for the counting assertions.
    const timedCount = 700;
    const timedParams = Array.from(
      { length: timedCount },
      (_, i) => new Parameter(`p${i}`, ['v0', 'v1']),
    );
    const timedTests = binaryCoveringSuite(timedCount);
    const timedTrivial = parseConstraint('p0 = v0 OR p0 = v1', timedParams);
    expect(timedTrivial.error.code).toBe(ErrorCode.Ok);
    const constraints = timedTrivial.constraint === null ? [] : [timedTrivial.constraint];

    const [unconstrainedMs, constrainedMs] = fastestEach(
      TIMING_RUNS,
      () => validateCoverage(timedParams, timedTests, 2),
      () => validateCoverage(timedParams, timedTests, 2, constraints),
    );

    // A tuple covered by a valid test already holds its own completion witness,
    // so it must never reach the feasibility search. That is binary: either the
    // witness is honoured and this suite — which covers its whole universe —
    // searches nothing, or it is not and every tuple pays for a descent over
    // every parameter.
    //
    // The bound detects that regime change rather than budgeting a runtime.
    // With the skip, constraints cost nothing measurable and the ratio sits at
    // 1.0; without it the same model runs 165x the unconstrained time, measured
    // by handing the validator an empty suite so that no tuple has a witness to
    // skip on. 5.0 is far enough above the ratio to clear contention and far
    // enough below the regression to leave it nowhere to hide.
    expect(constrainedMs).toBeLessThan(5.0 * unconstrainedMs);
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

  it('does not depend on how long the class names are', { timeout: MEASUREMENT_TIMEOUT_MS }, () => {
    // Resolving a value to its class runs once per (combination, test,
    // position), so it has to be a flat array read. A name-keyed lookup would
    // instead make the projection scale with how long the class labels happen
    // to be — a property of the model's vocabulary, not of its size.
    const parameterCount = 24;
    const values = 12;
    const classes = 4;
    // The suite size is what puts a run in the hundreds of milliseconds. This
    // gate has less signal than the others here, so it cannot also afford a
    // measurement small enough for contention to move.
    const suiteSize = 24000;
    // The two label lengths are what the gate contrasts, so the gap between
    // them is its signal. A name-keyed lookup compares label bytes on every
    // hit, so stretching the long labels to a quarter of the longest string
    // the model layer accepts is what carries that regression clear of the
    // bound; a flat array read never touches those bytes, so the honest ratio
    // does not move with them at all.
    const shortNames = paddedClassParams(parameterCount, values, classes, 2);
    const longNames = paddedClassParams(parameterCount, values, classes, MAX_STRING_BYTES / 4);
    const tests = spreadSuite(parameterCount, values, suiteSize);

    // Padding renames the classes without changing the class structure, so both
    // models must describe the same universe and the same coverage.
    const shortReport = computeClassCoverage(shortNames, tests, 2);
    const longReport = computeClassCoverage(longNames, tests, 2);
    expect(shortReport.error.code).toBe(ErrorCode.Ok);
    expect(longReport.totalClassTuples).toBe(shortReport.totalClassTuples);
    expect(longReport.coveredClassTuples).toBe(shortReport.coveredClassTuples);
    expect(longReport.coverageRatio).toBe(shortReport.coverageRatio);

    const [shortMs, longMs] = fastestEach(
      TIMING_RUNS,
      () => computeClassCoverage(shortNames, tests, 2),
      () => computeClassCoverage(longNames, tests, 2),
    );

    // A flat array read touches the same entries whatever the labels are, so
    // the honest ratio is 1.0, measured at 0.99. The bound separates that from
    // a name-keyed lookup over the same model, which measures 27.7: a lookup
    // hashes the label once per string and then compares its bytes on every
    // hit, and at this label length those comparisons dominate the projection.
    //
    // Both sides of the bound therefore have room — a third above the honest
    // ratio, more than an order of magnitude below the regression — so what
    // holds the gate is the separation itself rather than the precision of the
    // sampling. The length of the labels is what buys that separation, and it
    // is why they run to a quarter of the model layer's string limit instead of
    // a length that merely looks long: at 256 characters the same regression
    // measures 1.71 against the same bound, which leaves the gate reporting on
    // the machine as much as on the implementation.
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

  it('names the value the rejected row carried', () => {
    // A recorded row that names a value the model no longer declares keeps
    // UNASSIGNED at that position, so the caller's own text is the only thing
    // left that says which member of the row drifted. Naming the parameter
    // alone does not distinguish it from a row that omitted the member.
    const recorded = [new Parameter('browser', ['chrome', 'firefox']), new Parameter('os', ['x'])];
    const report = validateCoverage(
      recorded,
      [{ values: [UNASSIGNED, 0], unresolved: ['edge', ''] }, { values: [UNASSIGNED, 0] }],
      2,
    );

    expect(report.invalidTests).toEqual([
      { testIndex: 0, reason: "value 'edge' is not declared by parameter browser" },
      { testIndex: 1, reason: 'missing value for parameter browser' },
    ]);
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
