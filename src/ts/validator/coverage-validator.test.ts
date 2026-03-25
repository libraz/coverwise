import { describe, expect, it } from 'vitest';
import { Parameter } from '../model/parameter.js';
import type { GenerateResult, TestCase } from '../model/test-case.js';
import { createGenerateResult } from '../model/test-case.js';
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

  it('strength > params: vacuous coverage, ratio = 1.0', () => {
    const report = validateCoverage(params2x2, [], 5);
    expect(report.totalTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);
  });

  it('strength 0: vacuous coverage, ratio = 1.0', () => {
    const report = validateCoverage(params2x2, [], 0);
    expect(report.totalTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
    expect(report.uncovered).toHaveLength(0);
  });

  it('single parameter: strength=2 > 1 param, vacuous coverage', () => {
    const params = [new Parameter('os', ['win', 'mac', 'linux'])];
    const report = validateCoverage(params, [], 2);
    expect(report.totalTuples).toBe(0);
    expect(report.coverageRatio).toBe(1.0);
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

  it('no equivalence classes: returns zeros', () => {
    const params = [
      new Parameter('os', ['win', 'mac']),
      new Parameter('browser', ['chrome', 'safari']),
    ];

    const tests: TestCase[] = [{ values: [0, 0] }];
    const report = computeClassCoverage(params, tests, 2);
    expect(report.totalClassTuples).toBe(0);
    expect(report.coveredClassTuples).toBe(0);
    expect(report.coverageRatio).toBe(0);
  });

  it('strength 0: returns zeros', () => {
    const os = new Parameter('os', ['win', 'mac']);
    os.setEquivalenceClasses(['desktop', 'apple']);
    const report = computeClassCoverage([os], [], 0);
    expect(report.totalClassTuples).toBe(0);
    expect(report.coverageRatio).toBe(0);
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
