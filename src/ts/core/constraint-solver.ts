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

/** Smallest usable value index at or after `start`, or the domain size. */
function nextUsableValue(
  params: readonly Parameter[],
  allowedValues: readonly (readonly boolean[])[] | null,
  param: number,
  start: number,
): number {
  for (let vi = start; vi < params[param].size; ++vi) {
    if (allowedValues === null ? !params[param].isInvalid(vi) : allowedValues[param][vi]) {
      return vi;
    }
  }
  return params[param].size;
}

/**
 * Depth-first feasibility search driven by an explicit stack.
 *
 * The search depth grows with the parameter count, and a satisfiable chain
 * spends only one node of the budget per level, so the node budget alone does
 * not bound how deep the search goes. Keeping the frames in an array makes call
 * stack use independent of the model size while preserving the recursive
 * enumeration order, budget accounting and assignment side effects: on success
 * `assignment` holds the witness, and on failure every parameter this search
 * assigned is restored to UNASSIGNED.
 */
function search(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  assignment: number[],
  parameterOrder: SolveParameterOrder,
  orderPosition: number,
  allowedValues: readonly (readonly boolean[])[] | null,
  budget: SolveBudget,
): boolean {
  /** param: parameter assigned at this level; value: value currently tried. */
  const stack: Array<{ param: number; value: number; nextPosition: number }> = [];
  let position = orderPosition;
  let expand = true;

  for (;;) {
    if (expand) {
      expand = false;
      let dead = false;
      if (budget.remaining === 0) {
        budget.exceeded = true;
        dead = true;
      } else {
        --budget.remaining;
        dead = !constraintsCanStillPass(constraints, assignment);
      }
      if (!dead) {
        while (
          position < parameterOrder.length &&
          assignment[parameterOrder[position]] !== UNASSIGNED
        ) {
          ++position;
        }
        if (position === parameterOrder.length) {
          return true;
        }
        const next = parameterOrder[position];
        const vi = nextUsableValue(params, allowedValues, next, 0);
        if (vi < params[next].size) {
          assignment[next] = vi;
          ++position;
          stack.push({ param: next, value: vi, nextPosition: position });
          expand = true;
          continue;
        }
      }
    }

    // Backtrack. An exhausted budget unwinds without trying further values, so
    // the caller sees an untouched assignment together with budget.exceeded.
    const top = stack[stack.length - 1];
    if (top === undefined) {
      return false;
    }
    const vi = budget.exceeded
      ? params[top.param].size
      : nextUsableValue(params, allowedValues, top.param, top.value + 1);
    if (vi < params[top.param].size) {
      top.value = vi;
      assignment[top.param] = vi;
      position = top.nextPosition;
      expand = true;
      continue;
    }
    assignment[top.param] = UNASSIGNED;
    stack.pop();
  }
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
