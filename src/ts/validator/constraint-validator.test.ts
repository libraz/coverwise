import { describe, expect, it } from 'vitest';
import { EqualsNode, ImpliesNode, NotEqualsNode } from '../model/constraint-ast.js';
import type { TestCase } from '../model/test-case.js';
import { validateConstraintReport, validateConstraints } from './constraint-validator.js';

describe('validateConstraints', () => {
  // Params: os=[win(0), mac(1), linux(2)], browser=[chrome(0), firefox(1), ie(2)]
  // Constraint: IF os=mac THEN browser!=ie
  const macImpliesNotIe = new ImpliesNode(
    new EqualsNode(0, 1), // os=mac
    new NotEqualsNode(1, 2), // browser!=ie
  );

  it('no violations: all tests satisfy constraint', () => {
    const tests: TestCase[] = [
      { values: [0, 2] }, // win, ie -> antecedent false, constraint satisfied
      { values: [1, 0] }, // mac, chrome -> browser!=ie, satisfied
      { values: [1, 1] }, // mac, firefox -> browser!=ie, satisfied
      { values: [2, 2] }, // linux, ie -> antecedent false, satisfied
    ];

    const violations = validateConstraints(tests, [macImpliesNotIe]);
    expect(violations).toHaveLength(0);
  });

  it('single violation: one test violates', () => {
    const tests: TestCase[] = [
      { values: [0, 0] }, // win, chrome -> ok
      { values: [1, 2] }, // mac, ie -> VIOLATION
      { values: [2, 1] }, // linux, firefox -> ok
    ];

    const violations = validateConstraints(tests, [macImpliesNotIe]);
    expect(violations).toHaveLength(1);
    expect(violations[0].testIndex).toBe(1);
    expect(violations[0].constraintIndex).toBe(0);
    expect(violations[0].description).toContain('1');
  });

  it('multiple violations: multiple tests violate multiple constraints', () => {
    // Constraint 2: os != linux (param 0, value 2)
    const notLinux = new NotEqualsNode(0, 2);

    const tests: TestCase[] = [
      { values: [1, 2] }, // mac, ie -> violates constraint 0
      { values: [2, 0] }, // linux, chrome -> violates constraint 1
      { values: [2, 2] }, // linux, ie -> violates constraint 1 (not constraint 0 since os!=mac)
    ];

    const violations = validateConstraints(tests, [macImpliesNotIe, notLinux]);
    // test 0 violates constraint 0, test 1 violates constraint 1, test 2 violates constraint 1
    expect(violations).toHaveLength(3);

    const byTest = violations.map((v) => [v.testIndex, v.constraintIndex]);
    expect(byTest).toContainEqual([0, 0]);
    expect(byTest).toContainEqual([1, 1]);
    expect(byTest).toContainEqual([2, 1]);
  });

  it('empty constraints: no violations', () => {
    const tests: TestCase[] = [{ values: [1, 2] }];
    const violations = validateConstraints(tests, []);
    expect(violations).toHaveLength(0);
  });

  it('empty tests: no violations', () => {
    const violations = validateConstraints([], [macImpliesNotIe]);
    expect(violations).toHaveLength(0);
  });
});

describe('validateConstraintReport', () => {
  const macImpliesNotIe = new ImpliesNode(new EqualsNode(0, 1), new NotEqualsNode(1, 2));

  it('counts violations and violating indices', () => {
    const tests: TestCase[] = [
      { values: [0, 0] }, // ok
      { values: [1, 2] }, // violation
      { values: [1, 0] }, // ok
      { values: [1, 2] }, // violation
    ];

    const report = validateConstraintReport(tests, [macImpliesNotIe]);
    expect(report.totalTests).toBe(4);
    expect(report.violations).toBe(2);
    expect(report.violatingIndices).toEqual([1, 3]);
  });

  it('no violations: empty violating indices', () => {
    const tests: TestCase[] = [{ values: [0, 0] }, { values: [1, 0] }];

    const report = validateConstraintReport(tests, [macImpliesNotIe]);
    expect(report.totalTests).toBe(2);
    expect(report.violations).toBe(0);
    expect(report.violatingIndices).toHaveLength(0);
  });

  it('empty tests: totalTests=0, no violations', () => {
    const report = validateConstraintReport([], [macImpliesNotIe]);
    expect(report.totalTests).toBe(0);
    expect(report.violations).toBe(0);
    expect(report.violatingIndices).toHaveLength(0);
  });

  it('multiple constraints: test counted once even if violates multiple', () => {
    // os=mac AND browser=ie violates both constraints
    const alsoNotIe = new NotEqualsNode(1, 2); // browser!=ie as standalone

    const tests: TestCase[] = [
      { values: [1, 2] }, // mac, ie -> violates both
    ];

    const report = validateConstraintReport(tests, [macImpliesNotIe, alsoNotIe]);
    // Should count only once per test (breaks on first violation)
    expect(report.violations).toBe(1);
    expect(report.violatingIndices).toEqual([0]);
  });
});
