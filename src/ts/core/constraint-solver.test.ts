import { describe, expect, it } from 'vitest';
import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { parseConstraint } from '../model/constraint-parser.js';
import { ErrorCode } from '../model/error.js';
import { createGenerateOptions } from '../model/generate-options.js';
import { MAX_PARAMETERS, Parameter } from '../model/parameter.js';
import { UNASSIGNED } from '../model/test-case.js';
import {
  completeAssignment,
  completeValidAssignment,
  createSolveBudget,
} from './constraint-solver.js';
import { generate } from './generator.js';

/**
 * Constraint that stays undecided until every parameter is assigned. Nothing
 * can be pruned before a complete assignment, and the only satisfying one is
 * "all values at index 1", which the search reaches last, so proving it costs
 * more nodes than the budget allows.
 */
class CompleteAssignmentOnlyConstraint implements ConstraintNode {
  evaluate(assignment: number[]): ConstraintResult {
    if (assignment.some((value) => value === UNASSIGNED)) {
      return ConstraintResult.Unknown;
    }
    return assignment.every((value) => value === 1)
      ? ConstraintResult.True
      : ConstraintResult.False;
  }

  toString(): string {
    return 'complete-assignment-only';
  }
}

function binaryParams(count: number): Parameter[] {
  return Array.from({ length: count }, (_, index) => new Parameter(`P${index}`, ['a', 'b']));
}

function singleConstraint(expression: string, params: Parameter[]): ConstraintNode[] {
  const parse = parseConstraint(expression, params);
  expect(parse.constraint).toBeDefined();
  // biome-ignore lint/style/noNonNullAssertion: guarded by expect above.
  return [parse.constraint!];
}

describe('completeValidAssignment', () => {
  it('completes a model deeper than the call stack allows', () => {
    // A satisfiable chain spends one node of the budget per parameter, so a
    // recursive descent would exhaust the call stack long before the node
    // budget bites.
    const parameterCount = 200_000;
    const params = binaryParams(parameterCount);
    const constraints = singleConstraint('P0="a" OR P1="b"', params);
    const budget = createSolveBudget();

    const witness = completeValidAssignment(
      params,
      constraints,
      { values: new Array<number>(parameterCount).fill(UNASSIGNED) },
      budget,
    );

    expect(witness).not.toBeNull();
    expect(budget.exceeded).toBe(false);
    expect(witness?.values).toHaveLength(parameterCount);
    expect(witness?.values.every((value) => value !== UNASSIGNED)).toBe(true);
  });

  it('completes a deep model through the allowed-value mask', () => {
    const parameterCount = 200_000;
    const params = binaryParams(parameterCount);
    const constraints = singleConstraint('P0="a" OR P1="b"', params);
    const mask = params.map(() => [true, true]);
    const budget = createSolveBudget();

    const witness = completeAssignment(
      params,
      constraints,
      mask,
      { values: new Array<number>(parameterCount).fill(UNASSIGNED) },
      budget,
    );

    expect(witness).not.toBeNull();
    expect(budget.exceeded).toBe(false);
    expect(witness?.values.every((value) => value !== UNASSIGNED)).toBe(true);
  });

  it('finds a witness that requires backtracking', () => {
    const params = binaryParams(3);
    const constraints = singleConstraint('P0="b" AND P1="b" AND P2="b"', params);

    const witness = completeValidAssignment(params, constraints, {
      values: [UNASSIGNED, UNASSIGNED, UNASSIGNED],
    });

    expect(witness?.values).toEqual([1, 1, 1]);
  });

  it('keeps values the caller pinned in the witness', () => {
    const params = binaryParams(3);
    const constraints = singleConstraint('P1="b"', params);

    const witness = completeValidAssignment(params, constraints, {
      values: [1, UNASSIGNED, UNASSIGNED],
    });

    expect(witness?.values[0]).toBe(1);
    expect(witness?.values[1]).toBe(1);
    expect(witness?.values[2]).not.toBe(UNASSIGNED);
  });

  it('reports no witness for a contradictory model', () => {
    const params = binaryParams(3);
    const constraints = [
      ...singleConstraint('P0="a"', params),
      ...singleConstraint('P0="b"', params),
    ];
    const budget = createSolveBudget();

    const witness = completeValidAssignment(
      params,
      constraints,
      { values: [UNASSIGNED, UNASSIGNED, UNASSIGNED] },
      budget,
    );

    expect(witness).toBeNull();
    expect(budget.exceeded).toBe(false);
  });

  it('reports an exhausted budget instead of an unsatisfiable model', () => {
    const parameterCount = 24;
    const params = binaryParams(parameterCount);
    const budget = createSolveBudget();

    const witness = completeValidAssignment(
      params,
      [new CompleteAssignmentOnlyConstraint()],
      { values: new Array<number>(parameterCount).fill(UNASSIGNED) },
      budget,
    );

    expect(witness).toBeNull();
    expect(budget.exceeded).toBe(true);
    expect(budget.remaining).toBe(0);
  });
});

describe('generate parameter limit', () => {
  function binaryOptionParameters(count: number) {
    return Array.from({ length: count }, (_, index) => ({
      name: `P${index}`,
      values: ['a', 'b'],
    }));
  }

  it('rejects a parameter count beyond the documented limit', () => {
    // Generation completes an assignment per test case, one parameter per
    // search level, so an oversized model has to be turned away as invalid
    // input rather than handed to a search that cannot finish.
    const options = createGenerateOptions({
      parameters: binaryOptionParameters(200_000),
      constraintExpressions: ['P0="a" OR P1="b"'],
      strength: 1,
    });

    const result = generate(options);

    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.tests).toHaveLength(0);
  });

  it('accepts the largest documented parameter count', () => {
    const options = createGenerateOptions({
      parameters: binaryOptionParameters(MAX_PARAMETERS),
      constraintExpressions: ['P0="a" OR P1="b"'],
      strength: 1,
    });

    const result = generate(options);

    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.tests.length).toBeGreaterThan(0);
  });

  it('rejects one parameter past the limit', () => {
    const options = createGenerateOptions({
      parameters: binaryOptionParameters(MAX_PARAMETERS + 1),
      strength: 1,
    });

    const result = generate(options);

    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toBe('Parameter count 1025 exceeds maximum of 1024');
  });
});
