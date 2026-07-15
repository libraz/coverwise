/// Constraint feasibility search used by the generation core.

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import type { Parameter } from '../model/parameter.js';
import { type TestCase, UNASSIGNED } from '../model/test-case.js';

function constraintsCanStillPass(
  constraints: readonly ConstraintNode[],
  assignment: number[],
): boolean {
  return constraints.every(
    (constraint) => constraint.evaluate(assignment) !== ConstraintResult.False,
  );
}

function search(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  assignment: number[],
  assignedCount: number,
  allowedValues: readonly (readonly boolean[])[] | null,
): boolean {
  if (!constraintsCanStillPass(constraints, assignment)) {
    return false;
  }
  if (assignedCount === params.length) {
    return true;
  }

  let next = UNASSIGNED;
  let fewest = Number.MAX_SAFE_INTEGER;
  for (let pi = 0; pi < params.length; ++pi) {
    if (assignment[pi] !== UNASSIGNED) {
      continue;
    }
    let validCount = 0;
    for (let vi = 0; vi < params[pi].size; ++vi) {
      if (allowedValues === null ? !params[pi].isInvalid(vi) : allowedValues[pi][vi]) {
        ++validCount;
      }
    }
    if (validCount < fewest) {
      fewest = validCount;
      next = pi;
    }
  }
  if (next === UNASSIGNED || fewest === 0) {
    return false;
  }

  for (let vi = 0; vi < params[next].size; ++vi) {
    if (allowedValues === null ? params[next].isInvalid(vi) : !allowedValues[next][vi]) {
      continue;
    }
    assignment[next] = vi;
    if (search(params, constraints, assignment, assignedCount + 1, allowedValues)) {
      return true;
    }
  }
  assignment[next] = UNASSIGNED;
  return false;
}

/** Complete a partial assignment using a caller-provided allowed-value mask. */
export function completeAssignment(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  allowedValues: readonly (readonly boolean[])[],
  partial: TestCase,
): TestCase | null {
  if (
    allowedValues.length !== params.length ||
    allowedValues.some((allowed, pi) => allowed.length !== params[pi].size)
  ) {
    return null;
  }
  const assignment = new Array<number>(params.length).fill(UNASSIGNED);
  let assignedCount = 0;
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = partial.values[pi] ?? UNASSIGNED;
    assignment[pi] = vi;
    if (vi === UNASSIGNED) {
      continue;
    }
    if (vi < 0 || vi >= params[pi].size || !allowedValues[pi][vi]) {
      return null;
    }
    ++assignedCount;
  }
  return search(params, constraints, assignment, assignedCount, allowedValues)
    ? { values: assignment }
    : null;
}

/** Complete a partial assignment using valid parameter values. */
export function completeValidAssignment(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  partial: TestCase,
): TestCase | null {
  const assignment = new Array<number>(params.length).fill(UNASSIGNED);
  let assignedCount = 0;
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = partial.values[pi] ?? UNASSIGNED;
    assignment[pi] = vi;
    if (vi === UNASSIGNED) {
      continue;
    }
    if (vi < 0 || vi >= params[pi].size || params[pi].isInvalid(vi)) {
      return null;
    }
    ++assignedCount;
  }
  return search(params, constraints, assignment, assignedCount, null)
    ? { values: assignment }
    : null;
}
