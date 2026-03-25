/// Validate that generated test cases satisfy all constraints.

import type { ConstraintNode } from '../model/constraint-ast.js';
import { ConstraintResult as CR } from '../model/constraint-ast.js';
import type { TestCase } from '../model/test-case.js';

/** A single constraint violation record. */
export interface ConstraintViolation {
  /** Index of the violating test case. */
  testIndex: number;
  /** Index of the violated constraint. */
  constraintIndex: number;
  /** Human-readable description. */
  description: string;
}

/** Constraint validation report. */
export interface ConstraintReport {
  totalTests: number;
  violations: number;
  violatingIndices: number[];
}

/**
 * Validate that all test cases satisfy all constraints.
 *
 * Evaluates each constraint against each test case. A test case violates
 * a constraint if the constraint evaluates to False (fully assigned).
 * @returns Array of constraint violations (empty if all tests pass).
 */
export function validateConstraints(
  tests: TestCase[],
  constraints: ConstraintNode[],
): ConstraintViolation[] {
  const violations: ConstraintViolation[] = [];

  for (let i = 0; i < tests.length; ++i) {
    for (let j = 0; j < constraints.length; ++j) {
      const result = constraints[j].evaluate(tests[i].values);
      if (result === CR.False) {
        violations.push({
          testIndex: i,
          constraintIndex: j,
          description: `Test case ${i} violates constraint ${j}`,
        });
      }
    }
  }

  return violations;
}

/**
 * Validate constraints and return a summary report.
 *
 * Unlike validateConstraints which returns per-violation details, this function
 * returns an aggregate report matching the C++ ConstraintReport structure.
 * A test case is counted as violating at most once (on first violated constraint).
 */
export function validateConstraintReport(
  tests: TestCase[],
  constraints: ConstraintNode[],
): ConstraintReport {
  const report: ConstraintReport = {
    totalTests: tests.length,
    violations: 0,
    violatingIndices: [],
  };

  for (let i = 0; i < tests.length; ++i) {
    for (const constraint of constraints) {
      const result = constraint.evaluate(tests[i].values);
      if (result === CR.False) {
        report.violations++;
        report.violatingIndices.push(i);
        break;
      }
    }
  }

  return report;
}
