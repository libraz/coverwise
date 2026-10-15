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

/**
 * Reusable frame storage for a feasibility search.
 *
 * Levels are kept flat -- three numbers per level, `[param, value,
 * nextPosition]` -- so descending a level costs no frame object. The search
 * depth is bounded by the parameter count, so one buffer sized for the model
 * serves every search over it: a caller that solves many partial assignments,
 * one per tuple for instance, owns a single stack and passes it in. A search
 * only ever reads back what it wrote during that same search, so whatever a
 * previous one left behind cannot change any result.
 */
export type SolveStack = number[];

export function createSolveStack(): SolveStack {
  return [];
}

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
 *
 * `stack` is scratch owned by the caller; see SolveStack for its layout.
 */
function search(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  assignment: number[],
  parameterOrder: SolveParameterOrder,
  orderPosition: number,
  allowedValues: readonly (readonly boolean[])[] | null,
  budget: SolveBudget,
  stack: SolveStack,
): boolean {
  /** Levels in use; the slots beyond them hold whatever an earlier search left. */
  let depth = 0;
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
          const base = depth * 3;
          stack[base] = next;
          stack[base + 1] = vi;
          stack[base + 2] = position;
          ++depth;
          expand = true;
          continue;
        }
      }
    }

    // Backtrack. An exhausted budget unwinds without trying further values, so
    // the caller sees an untouched assignment together with budget.exceeded.
    if (depth === 0) {
      return false;
    }
    const base = (depth - 1) * 3;
    const topParam = stack[base];
    const vi = budget.exceeded
      ? params[topParam].size
      : nextUsableValue(params, allowedValues, topParam, stack[base + 1] + 1);
    if (vi < params[topParam].size) {
      stack[base + 1] = vi;
      assignment[topParam] = vi;
      position = stack[base + 2];
      expand = true;
      continue;
    }
    assignment[topParam] = UNASSIGNED;
    --depth;
  }
}

function isAllowedValueMaskValid(
  params: readonly Parameter[],
  allowedValues: readonly (readonly boolean[])[],
): boolean {
  return (
    allowedValues.length === params.length &&
    !allowedValues.some((allowed, pi) => allowed.length !== params[pi].size)
  );
}

function hasValidPinnedValues(
  params: readonly Parameter[],
  values: readonly number[],
  allowedValues: readonly (readonly boolean[])[] | null,
): boolean {
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = values[pi] ?? UNASSIGNED;
    if (vi === UNASSIGNED) {
      continue;
    }
    if (
      vi < 0 ||
      vi >= params[pi].size ||
      (allowedValues === null ? params[pi].isInvalid(vi) : !allowedValues[pi][vi])
    ) {
      return false;
    }
  }
  return true;
}

function normalizeUnassignedValues(values: number[]): void {
  for (let pi = 0; pi < values.length; ++pi) {
    values[pi] ??= UNASSIGNED;
  }
}

/**
 * Complete an assignment in its existing values buffer using an allowed-value
 * mask. `assignment.values` must have exactly one slot per parameter. The
 * buffer is scratch on failure: caller-provided pins remain, while values
 * assigned by the search are restored to `UNASSIGNED`.
 */
export function completeAssignmentInPlace(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  allowedValues: readonly (readonly boolean[])[],
  assignment: TestCase,
  budget?: SolveBudget,
  parameterOrder?: SolveParameterOrder,
  stack?: SolveStack,
): boolean {
  if (
    assignment.values.length !== params.length ||
    !isAllowedValueMaskValid(params, allowedValues) ||
    !hasValidPinnedValues(params, assignment.values, allowedValues)
  ) {
    return false;
  }
  normalizeUnassignedValues(assignment.values);

  const b = budget ?? createSolveBudget();
  const order = parameterOrder ?? buildAllowedSolveParameterOrder(params, allowedValues);
  return search(params, constraints, assignment.values, order, 0, allowedValues, b, stack ?? []);
}

/**
 * Complete an assignment in its existing values buffer using valid parameter
 * values. `assignment.values` must have exactly one slot per parameter. The
 * buffer is scratch on failure: caller-provided pins remain, while values
 * assigned by the search are restored to `UNASSIGNED`.
 */
export function completeValidAssignmentInPlace(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  assignment: TestCase,
  budget?: SolveBudget,
  parameterOrder?: SolveParameterOrder,
  stack?: SolveStack,
): boolean {
  if (
    assignment.values.length !== params.length ||
    !hasValidPinnedValues(params, assignment.values, null)
  ) {
    return false;
  }
  normalizeUnassignedValues(assignment.values);

  const b = budget ?? createSolveBudget();
  const order = parameterOrder ?? buildValidSolveParameterOrder(params);
  return search(params, constraints, assignment.values, order, 0, null, b, stack ?? []);
}

/**
 * Complete a partial assignment using a caller-provided allowed-value mask.
 *
 * The search is node-bounded (see SolveBudget). If `budget` is provided and the
 * budget is exhausted, `budget.exceeded` is set and the function returns null;
 * pass none to use a private default budget and ignore the signal. Pass `stack`
 * to reuse one frame buffer across searches; leaving it out allocates a private
 * one. The choice affects allocation only, never the outcome.
 */
export function completeAssignment(
  params: readonly Parameter[],
  constraints: readonly ConstraintNode[],
  allowedValues: readonly (readonly boolean[])[],
  partial: TestCase,
  budget?: SolveBudget,
  parameterOrder?: SolveParameterOrder,
  stack?: SolveStack,
): TestCase | null {
  const assignment = { values: new Array<number>(params.length) };
  for (let pi = 0; pi < params.length; ++pi) {
    assignment.values[pi] = partial.values[pi] ?? UNASSIGNED;
  }
  return completeAssignmentInPlace(
    params,
    constraints,
    allowedValues,
    assignment,
    budget,
    parameterOrder,
    stack,
  )
    ? assignment
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
  stack?: SolveStack,
): TestCase | null {
  const assignment = { values: new Array<number>(params.length) };
  for (let pi = 0; pi < params.length; ++pi) {
    assignment.values[pi] = partial.values[pi] ?? UNASSIGNED;
  }
  return completeValidAssignmentInPlace(
    params,
    constraints,
    assignment,
    budget,
    parameterOrder,
    stack,
  )
    ? assignment
    : null;
}
