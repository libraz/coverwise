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
  createSolveStack,
  type SolveBudget,
  type SolveStack,
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

/// Parse an expression a test depends on, failing the test if it does not parse.
///
/// `ParseResult.constraint` is null on a parse failure, and null passes
/// `toBeDefined()`, so a guard written against `undefined` lets a failed parse
/// through as the solver input the case was meant to exclude.
function singleConstraint(expression: string, params: Parameter[]): ConstraintNode[] {
  const parse = parseConstraint(expression, params);
  if (parse.constraint === null) {
    throw new Error(`Constraint did not parse: ${expression} -- ${parse.error.message}`);
  }
  return [parse.constraint];
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

describe('a reused frame buffer', () => {
  /** One search outcome, recorded in full so two runs can be compared. */
  interface SearchOutcome {
    witness: number[] | null;
    remaining: number;
    exceeded: boolean;
  }

  /**
   * Solve every pair of pinned values in the model, one search per pair. This is
   * the shape the coverage sweep drives: a long run of searches over one model,
   * each starting from a different two-parameter partial assignment.
   *
   * @param stack Frame buffer shared across the run, or undefined for a private
   *   one per search.
   */
  function solveEveryPair(
    params: Parameter[],
    constraints: ConstraintNode[],
    stack: SolveStack | undefined,
  ): SearchOutcome[] {
    const outcomes: SearchOutcome[] = [];
    for (let left = 0; left < params.length; ++left) {
      for (let right = left + 1; right < params.length; ++right) {
        for (let lv = 0; lv < params[left].size; ++lv) {
          for (let rv = 0; rv < params[right].size; ++rv) {
            const values = new Array<number>(params.length).fill(UNASSIGNED);
            values[left] = lv;
            values[right] = rv;
            const budget = createSolveBudget();
            const witness = completeValidAssignment(
              params,
              constraints,
              { values },
              budget,
              undefined,
              stack,
            );
            outcomes.push({
              witness: witness === null ? null : witness.values,
              remaining: budget.remaining,
              exceeded: budget.exceeded,
            });
          }
        }
      }
    }
    return outcomes;
  }

  function constrainedModel(): { params: Parameter[]; constraints: ConstraintNode[] } {
    const params = binaryParams(9);
    const expressions = [
      'IF P0="a" THEN P1="b"',
      'IF P1="b" THEN P2="a"',
      'IF P2="a" THEN P3="b"',
      'P3="a" OR P4="a"',
      'NOT (P5="a" AND P6="a")',
      'IF P7="b" THEN P8="b"',
    ];
    return {
      params,
      constraints: expressions.flatMap((expression) => singleConstraint(expression, params)),
    };
  }

  // A frame buffer handed in by the caller is scratch and nothing else: every
  // search reports the same verdict, the same witness and the same budget
  // arithmetic whether it got a buffer of its own or one a previous search left
  // behind. Interacting implications make some of these pairs infeasible and
  // others reachable only after backtracking, so the run exercises both exits.
  it('does not change any search outcome', () => {
    const fresh = constrainedModel();
    const reused = constrainedModel();

    const withPrivateBuffers = solveEveryPair(fresh.params, fresh.constraints, undefined);
    const withOneBuffer = solveEveryPair(reused.params, reused.constraints, createSolveStack());

    expect(withOneBuffer).toEqual(withPrivateBuffers);
    // The run has to contain both verdicts, or it fixes nothing.
    expect(withPrivateBuffers.some((outcome) => outcome.witness !== null)).toBe(true);
    expect(withPrivateBuffers.some((outcome) => outcome.witness === null)).toBe(true);
  });

  // The budget-exhausted exit leaves levels in the buffer that no backtrack
  // pops. Reusing it for the next search must still report an exhausted budget
  // and an untouched assignment rather than resuming from the leftovers.
  it('is not resumed after a search that exhausted its budget', () => {
    const parameterCount = 24;
    const params = binaryParams(parameterCount);
    const constraints = [new CompleteAssignmentOnlyConstraint()];
    const shared = createSolveStack();

    for (let attempt = 0; attempt < 2; ++attempt) {
      // A budget far below the default still runs out mid-descent, which is the
      // exit under test, and keeps the run short enough to repeat.
      const budget: SolveBudget = { remaining: 10_000, exceeded: false };
      const values = new Array<number>(parameterCount).fill(UNASSIGNED);

      const witness = completeValidAssignment(
        params,
        constraints,
        { values },
        budget,
        undefined,
        shared,
      );

      expect(witness).toBeNull();
      expect(budget.exceeded).toBe(true);
      expect(budget.remaining).toBe(0);
    }
  });

  // The masked entry point reaches the search through a different argument path.
  it('does not change a masked search', () => {
    const fresh = constrainedModel();
    const reused = constrainedModel();
    const mask = fresh.params.map(() => [true, true]);
    const shared = createSolveStack();

    for (let pi = 0; pi < fresh.params.length; ++pi) {
      for (let vi = 0; vi < fresh.params[pi].size; ++vi) {
        const freshValues = new Array<number>(fresh.params.length).fill(UNASSIGNED);
        freshValues[pi] = vi;
        const reusedValues = [...freshValues];
        const freshBudget = createSolveBudget();
        const reusedBudget = createSolveBudget();

        const freshWitness = completeAssignment(
          fresh.params,
          fresh.constraints,
          mask,
          { values: freshValues },
          freshBudget,
          undefined,
          undefined,
        );
        const reusedWitness = completeAssignment(
          reused.params,
          reused.constraints,
          mask,
          { values: reusedValues },
          reusedBudget,
          undefined,
          shared,
        );

        expect(reusedWitness).toEqual(freshWitness);
        expect(reusedBudget.remaining).toBe(freshBudget.remaining);
        expect(reusedBudget.exceeded).toBe(freshBudget.exceeded);
      }
    }
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
