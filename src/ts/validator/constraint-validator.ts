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
 * Validate that all test cases satisfy all constraints (per-violation detail).
 *
 * Evaluates each constraint against each test case and emits one record per
 * (test, violated constraint) pair, so a test that violates two constraints
 * yields two records. This per-violation detail is a TypeScript-only convenience
 * and is NOT the canonical cross-surface shape: use validateConstraintReport
 * for the aggregate report that agrees with the C++ ValidateConstraints (where a
 * test is counted at most once). The WASM binding exports only the aggregate.
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
          description: `Test case ${i} violates constraint ${j}: ${constraints[j].toString()}`,
        });
      }
    }
  }

  return violations;
}

/**
 * Validate constraints and return a summary report (canonical cross-surface).
 *
 * Unlike validateConstraints which returns per-violation details, this function
 * returns an aggregate report matching the C++ ConstraintReport structure
 * field-for-field with identical per-test semantics: a test case is counted as
 * violating at most once (on its first violated constraint). This is the
 * canonical report shape shared by every surface.
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
