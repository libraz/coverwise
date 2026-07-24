/// Constraint feasibility search used by the generation core.

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import type { Parameter } from '../model/parameter.js';
import { type TestCase, UNASSIGNED } from '../model/test-case.js';

/**
 * Default recursion-node budget for a single feasibility search.
 *
 * A contradictory or otherwise hard model can make backtracking exponential.
 * Bounding each search guarantees termination; satisfiable models resolve far
 * below the limit under fail-first ordering. Mirrors kDefaultSolveNodeBudget in
 * the C++ core.
 */
export const DEFAULT_SOLVE_NODE_BUDGET = 2_000_000;

/**
 * Budget and outcome for a bounded feasibility search. When `remaining` reaches
 * zero the search aborts and sets `exceeded`, so the caller can surface an
 * explicit error instead of silently treating the model as unsatisfiable.
 */
export interface SolveBudget {
  remaining: number;
  exceeded: boolean;
}

export function createSolveBudget(): SolveBudget {
  return { remaining: DEFAULT_SOLVE_NODE_BUDGET, exceeded: false };
}

export type SolveParameterOrder = readonly number[];

function buildSolveParameterOrder(
  params: readonly Parameter[],
  allowedValues: readonly (readonly boolean[])[] | null,
): number[] {
  return params
    .map((param, pi) => {
      let count = 0;
      for (let vi = 0; vi < param.size; ++vi) {
        if (allowedValues === null ? !param.isInvalid(vi) : allowedValues[pi][vi]) {
          ++count;
        }
      }
      return { pi, count };
    })
    .sort((left, right) => left.count - right.count || left.pi - right.pi)
    .map(({ pi }) => pi);
}

export function buildValidSolveParameterOrder(params: readonly Parameter[]): number[] {
  return buildSolveParameterOrder(params, null);
}

export function buildAllowedSolveParameterOrder(
  params: readonly Parameter[],
  allowedValues: readonly (readonly boolean[])[],
): number[] {
  return buildSolveParameterOrder(params, allowedValues);
}

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
  parameterOrder: SolveParameterOrder,
  orderPosition: number,
  allowedValues: readonly (readonly boolean[])[] | null,
  budget: SolveBudget,
): boolean {
  if (budget.remaining === 0) {
    budget.exceeded = true;
    return false;
  }
  --budget.remaining;
  if (!constraintsCanStillPass(constraints, assignment)) {
    return false;
  }
  while (
    orderPosition < parameterOrder.length &&
    assignment[parameterOrder[orderPosition]] !== UNASSIGNED
  ) {
    ++orderPosition;
  }
  if (orderPosition === parameterOrder.length) {
    return true;
  }
  const next = parameterOrder[orderPosition];

  for (let vi = 0; vi < params[next].size; ++vi) {
    if (allowedValues === null ? params[next].isInvalid(vi) : !allowedValues[next][vi]) {
      continue;
    }
    assignment[next] = vi;
    if (
      search(
        params,
        constraints,
        assignment,
        parameterOrder,
        orderPosition + 1,
        allowedValues,
        budget,
      )
    ) {
      return true;
    }
    if (budget.exceeded) {
      assignment[next] = UNASSIGNED;
      return false;
    }
  }
  assignment[next] = UNASSIGNED;
  return false;
}

/**
 * Complete a partial assignment using a caller-provided allowed-value mask.
 *
 * The search is node-bounded (see SolveBudget). If `budget` is provided and the
 * budget is exhausted, `budget.exceeded` is set and the function returns null;
 * pass none to use a private default budget and ignore the signal.
 */
export function completeAssignment(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  allowedValues: readonly (readonly boolean[])[],
  partial: TestCase,
  budget?: SolveBudget,
  parameterOrder?: SolveParameterOrder,
): TestCase | null {
  if (
    allowedValues.length !== params.length ||
    allowedValues.some((allowed, pi) => allowed.length !== params[pi].size)
  ) {
    return null;
  }
  const assignment = new Array<number>(params.length).fill(UNASSIGNED);
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = partial.values[pi] ?? UNASSIGNED;
    assignment[pi] = vi;
    if (vi === UNASSIGNED) {
      continue;
    }
    if (vi < 0 || vi >= params[pi].size || !allowedValues[pi][vi]) {
      return null;
    }
  }
  const b = budget ?? createSolveBudget();
  const order = parameterOrder ?? buildAllowedSolveParameterOrder(params, allowedValues);
  return search(params, constraints, assignment, order, 0, allowedValues, b)
    ? { values: assignment }
    : null;
}

/**
 * Complete a partial assignment using valid parameter values. The search is
 * bounded; see completeAssignment for the `budget` semantics.
 */
export function completeValidAssignment(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  partial: TestCase,
  budget?: SolveBudget,
  parameterOrder?: SolveParameterOrder,
): TestCase | null {
  const assignment = new Array<number>(params.length).fill(UNASSIGNED);
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = partial.values[pi] ?? UNASSIGNED;
    assignment[pi] = vi;
    if (vi === UNASSIGNED) {
      continue;
    }
    if (vi < 0 || vi >= params[pi].size || params[pi].isInvalid(vi)) {
      return null;
    }
  }
  const b = budget ?? createSolveBudget();
  const order = parameterOrder ?? buildValidSolveParameterOrder(params);
  return search(params, constraints, assignment, order, 0, null, b) ? { values: assignment } : null;
}
