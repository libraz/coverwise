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
import { MAX_CLASS_TUPLE_SEARCH_NODES, MAX_SEARCH_NODES } from '../model/tuning-limits.js';
import {
  annotateClassCoverage,
  computeClassCoverage,
  validateCoverage,
} from './coverage-validator.js';

/// Parse an expression a test depends on, failing the test if it does not parse.
///
/// `ParseResult.constraint` is null on a parse failure, and null passes both
/// `toBeDefined()` and a `!== undefined` guard. A test written against
/// `undefined` therefore stays green while handing the validator a shorter
/// constraint list than the case describes, which is the one thing a ground-truth
/// test must never do. The C++ tests assert on `parse.error` for the same reason;
/// this is that check, in the form the call sites need.
function requireConstraint(expression: string, params: Parameter[]): ConstraintNode {
  const parsed = parseConstraint(expression, params);
  if (parsed.constraint === null) {
    throw new Error(`Constraint did not parse: ${expression} -- ${parsed.error.message}`);
  }
  return parsed.constraint;
}

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
    const constraints = ['os=win', 'os!=win'].map((expression) =>
      requireConstraint(expression, params2x2),
    );
    const report = validateCoverage(params2x2, [], 2, constraints);
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
    const report = validateCoverage(params, [], 1, [requireConstraint('P0="a" OR P1="b"', params)]);

    expect(report.error.code).toBe(ErrorCode.InvalidInput);
    expect(report.totalTuples).toBe(0);
  });

  it('accepts the largest documented parameter count', () => {
    const params = Array.from(
      { length: MAX_PARAMETERS },
      (_, index) => new Parameter(`P${index}`, ['a', 'b']),
    );
    const report = validateCoverage(params, [], 1, [requireConstraint('P0="a" OR P1="b"', params)]);

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

    const constraint = requireConstraint('IF os=mac THEN browser!=ie', params);

    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome -> desktop, modern
      { values: [0, 1] }, // win, ie     -> desktop, legacy
      { values: [1, 0] }, // mac, chrome -> apple, modern
    ];

    const report = computeClassCoverage(params, tests, 2, [constraint]);
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

/**
 * Satisfiable only through the first value of the first parameter, and undecided
 * until the last parameter is assigned.
 *
 * A representative that pins the first parameter anywhere else therefore has to
 * walk every branch below it before the contradiction shows up, which makes the
 * node count of its search a plain function of the domain sizes. Every search
 * node asks once, so the count of questions is the count of nodes spent.
 */
class LateContradictionConstraint implements ConstraintNode {
  evaluations = 0;

  evaluate(assignment: number[]): ConstraintResult {
    ++this.evaluations;
    if (assignment[0] === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (assignment[0] === 0) {
      return ConstraintResult.True;
    }
    return assignment[assignment.length - 1] === UNASSIGNED
      ? ConstraintResult.Unknown
      : ConstraintResult.False;
  }

  toString(): string {
    return 'late-contradiction';
  }
}

/** Parameter with `size` generated values and no classes. */
function fanParam(name: string, size: number): Parameter {
  return new Parameter(
    name,
    Array.from({ length: size }, (_, index) => `${name}${index}`),
  );
}

// Fan sizes chosen so one representative search under LateContradictionConstraint
// spends exactly one search budget: the search spends one node on the root and
// one on every prefix it extends the root into, 1 + |a| + |a||b| + |a||b||c|.
// They are a factorisation of the budget and must be re-derived if it changes.
const FAN_A = 17;
const FAN_B = 118;
const FAN_C = 996;

/** How many of those searches the shared class-tuple total pays for. */
const BUDGET_SHARE = MAX_CLASS_TUPLE_SEARCH_NODES / MAX_SEARCH_NODES;

/**
 * Model whose "bad" class holds `badValues` representatives, each of which costs
 * exactly one search budget to reject.
 *
 * The count is what decides where the shared total runs out: at BUDGET_SHARE the
 * enumeration ends on the search that drains it, and every representative
 * reached a verdict; above it the total is gone with representatives still to
 * come, and those are genuinely undecided.
 */
function exactBudgetModel(badValues: number): Parameter[] {
  const gateValues = ['ok'];
  const gateClasses = ['escape'];
  for (let index = 0; index < badValues; ++index) {
    gateValues.push(`bad${index}`);
    gateClasses.push('bad');
  }
  const gate = new Parameter('gate', gateValues);
  gate.setEquivalenceClasses(gateClasses);
  return [gate, fanParam('a', FAN_A), fanParam('b', FAN_B), fanParam('c', FAN_C)];
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

  it('reports a tuple decided on its last node as infeasible, not undecided', () => {
    // Draining the shared total is not by itself evidence that anything went
    // unanswered: a search can spend every node it was granted and still reach a
    // verdict. This model is sized so that is exactly what happens — each
    // representative of the "bad" class walks a tree of exactly one search budget
    // worth of nodes and rejects, and the last of them takes the shared total to
    // zero, with every representative searched to the end.
    expect(1 + FAN_A + FAN_A * FAN_B + FAN_A * FAN_B * FAN_C).toBe(MAX_SEARCH_NODES);
    expect(BUDGET_SHARE * MAX_SEARCH_NODES).toBe(MAX_CLASS_TUPLE_SEARCH_NODES);

    const params = exactBudgetModel(BUDGET_SHARE);
    const constraint = new LateContradictionConstraint();
    // A valid row through the escape value, so only the "bad" class tuple is
    // searched and the whole shared total is available to it.
    const tests: TestCase[] = [{ values: [0, 0, 0, 0] }];

    const report = computeClassCoverage(params, tests, 1, [constraint]);

    expect(report.error.code, `${report.error.message}: ${report.error.detail}`).toBe(ErrorCode.Ok);
    expect(report.totalClassTuples).toBe(1);
    expect(report.coveredClassTuples).toBe(1);
    expect(report.coverageRatio).toBe(1);
    // The verdict above only means anything if the shared total really did run
    // out: one question per node spent, so this is the sizing holding.
    expect(constraint.evaluations).toBeGreaterThanOrEqual(MAX_CLASS_TUPLE_SEARCH_NODES);
  }, 60_000);

  it('leaves a tuple undecided when a representative goes unsearched', () => {
    // The model above with one more representative than the shared total pays
    // for. The total is gone with a representative still to come, so that one
    // never got an answer and the tuple is undecidable — the neighbouring input
    // to the test above, and the side an over-eager "everything was decided"
    // would wrongly claim as infeasible. Under-reporting a tuple here would drop
    // it from the coverage universe and hide that nothing covers it.
    const params = exactBudgetModel(BUDGET_SHARE + 1);
    const tests: TestCase[] = [{ values: [0, 0, 0, 0] }];

    const report = computeClassCoverage(params, tests, 1, [new LateContradictionConstraint()]);

    expect(report.error.code).toBe(ErrorCode.ConstraintError);
    expect(report.error.message).toBe('Constraint search budget exceeded');
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
      const constraint = requireConstraint(COSTLY_REPRESENTATIVE_EXPRESSION, params);
      const result = createGenerateResult();

      annotateClassCoverage(result, params, 2, [constraint]);

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
    const constraints = ['IF A=0 THEN C=0', 'IF B=0 THEN C=1'].map((expression) =>
      requireConstraint(expression, params),
    );
    const report = validateCoverage(
      params,
      [{ values: [0, 1, 0] }, { values: [1, 0, 1] }, { values: [1, 1, 0] }],
      2,
      constraints,
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
    const report = validateCoverage(params, [{ values: [0, 0] }, { values: [0, 0, 1] }], 2, [
      requireConstraint('IF A=0 THEN C=0', params),
    ]);
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
/// looser estimate.
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

describe('class projection', () => {
  /**
   * Parameters whose class labels are padded to `nameLength` characters, and a
   * count of how many times any of those labels is read.
   *
   * The padding goes in front, so labels agree on everything but their last
   * character. The label array is handed to the parameter behind a proxy, so
   * every element read is counted however it is reached: through
   * `equivalenceClass`, through the array getter, or by iterating it.
   */
  function countingClassParams(
    parameterCount: number,
    values: number,
    classes: number,
    nameLength: number,
  ): { params: Parameter[]; labelReads: () => number } {
    let reads = 0;
    const params = Array.from({ length: parameterCount }, (_, index) => {
      const parameter = new Parameter(
        `p${index}`,
        Array.from({ length: values }, (_, value) => `v${value}`),
      );
      const labels = Array.from({ length: values }, (_, value) =>
        `c${value % classes}`.padStart(nameLength, 'x'),
      );
      parameter.setEquivalenceClasses(
        new Proxy(labels, {
          get(target, key, receiver) {
            if (typeof key === 'string' && /^\d+$/.test(key)) {
              reads += 1;
            }
            return Reflect.get(target, key, receiver);
          },
        }),
      );
      return parameter;
    });
    return { params, labelReads: () => reads };
  }

  function spreadSuite(parameterCount: number, values: number, count: number): TestCase[] {
    return Array.from({ length: count }, (_, t) => ({
      values: Array.from({ length: parameterCount }, (_, index) => (t * 7 + index * 5) % values),
    }));
  }

  const parameterCount = 12;
  const values = 12;
  const classes = 4;
  const labelLength = MAX_STRING_BYTES / 4;

  it('reads the class labels a number of times set by the model, not by the suite', () => {
    // Resolving a value to its class runs once per (combination, test,
    // position), so it has to be a flat array read into the class domain the
    // validator interned up front. A lookup keyed by the label instead reaches
    // the label text on every projection, which makes the work scale with the
    // model's vocabulary rather than its size — and, unlike the cost of that
    // work, the count of those reads is a function of the input alone.
    const small = countingClassParams(parameterCount, values, classes, labelLength);
    const large = countingClassParams(parameterCount, values, classes, labelLength);

    const smallReport = computeClassCoverage(
      small.params,
      spreadSuite(parameterCount, values, 20),
      2,
    );
    const largeReport = computeClassCoverage(
      large.params,
      spreadSuite(parameterCount, values, 4000),
      2,
    );
    expect(smallReport.error.code).toBe(ErrorCode.Ok);
    expect(largeReport.error.code).toBe(ErrorCode.Ok);
    expect(largeReport.totalClassTuples).toBe(smallReport.totalClassTuples);

    // Two hundred times the rows project through the same class domain, so the
    // labels are read exactly as often for one suite as for the other.
    expect(large.labelReads()).toBe(small.labelReads());
    // And that shared count is the domain build itself, one read per declared
    // value with nothing per combination or per row on top. A lookup keyed by
    // the label would instead read one per (combination, test, position), which
    // is two orders of magnitude more for the smaller of the two suites alone.
    expect(small.labelReads()).toBeLessThanOrEqual(2 * parameterCount * values);
  });

  it('describes the same class universe whatever the labels are', () => {
    // Padding renames the classes without changing the class structure, so both
    // models must describe the same universe and cover the same part of it. The
    // long labels agree on every character but their last, so a class identity
    // that settles on a bounded prefix would merge the four into one here.
    const shortNames = countingClassParams(parameterCount, values, classes, 2).params;
    const longNames = countingClassParams(parameterCount, values, classes, labelLength).params;
    const tests = spreadSuite(parameterCount, values, 400);

    const shortReport = computeClassCoverage(shortNames, tests, 2);
    const longReport = computeClassCoverage(longNames, tests, 2);
    expect(shortReport.error.code).toBe(ErrorCode.Ok);
    expect(longReport.error.code).toBe(ErrorCode.Ok);

    const expectedClassTuples = ((parameterCount * (parameterCount - 1)) / 2) * classes * classes;
    expect(shortReport.totalClassTuples).toBe(expectedClassTuples);
    expect(longReport.totalClassTuples).toBe(expectedClassTuples);
    expect(shortReport.coveredClassTuples).toBeGreaterThan(0);
    expect(longReport.coveredClassTuples).toBe(shortReport.coveredClassTuples);
    expect(longReport.coverageRatio).toBe(shortReport.coverageRatio);
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
    return [requireConstraint(expression, params)];
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
