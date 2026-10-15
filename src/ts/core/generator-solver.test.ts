import { ErrorCode } from '../model/error.js';
import { createGenerateOptions, type GenerateOptions } from '../model/generate-options.js';
import type { TestCase } from '../model/test-case.js';
import type { SolveBudget, SolveParameterOrder, SolveStack } from './constraint-solver.js';
import { generate } from './generator.js';

/**
 * Observation hooks around the feasibility solver.
 *
 * The generator reaches the solver only through this module, so wrapping it is
 * the cheapest way to watch how often the parameter order is rebuilt and to
 * drive the budget-exhausted exits without a model that really costs two
 * million search nodes.
 */
const hooks = vi.hoisted(() => ({
  /** completeAssignment calls that supplied a prebuilt parameter order. */
  callsWithOrder: 0,
  /** completeAssignment calls that left the solver to rebuild the order. */
  callsWithoutOrder: 0,
  /** Feasibility calls that received a reusable frame stack. */
  callsWithStack: 0,
  /** Feasibility calls that allocated their own frame stack. */
  callsWithoutStack: 0,
  /** Stack identities observed across calls in one generation pass. */
  stackRefs: [] as unknown[],
  validCalls: 0,
  /** Value index of the parameter-0 value that marks a negative-phase mask. */
  exhaustOnFixedValue: -1,
  reset(): void {
    hooks.callsWithOrder = 0;
    hooks.callsWithoutOrder = 0;
    hooks.callsWithStack = 0;
    hooks.callsWithoutStack = 0;
    hooks.stackRefs = [];
    hooks.validCalls = 0;
    hooks.exhaustOnFixedValue = -1;
  },
}));

vi.mock('./constraint-solver.js', async (importOriginal) => {
  const actual = await importOriginal<typeof import('./constraint-solver.js')>();
  return {
    ...actual,
    completeAssignmentInPlace: (
      params: Parameters<typeof actual.completeAssignmentInPlace>[0],
      constraints: Parameters<typeof actual.completeAssignmentInPlace>[1],
      allowedValues: readonly (readonly boolean[])[],
      assignment: TestCase,
      budget?: SolveBudget,
      parameterOrder?: SolveParameterOrder,
      stack?: SolveStack,
    ) => {
      const fixed = allowedValues[0]?.filter(Boolean).length === 1;
      if (fixed && hooks.exhaustOnFixedValue >= 0 && allowedValues[0][hooks.exhaustOnFixedValue]) {
        if (budget !== undefined) {
          budget.remaining = 0;
          budget.exceeded = true;
        }
        return false;
      }
      return actual.completeAssignmentInPlace(
        params,
        constraints,
        allowedValues,
        assignment,
        budget,
        parameterOrder,
        stack,
      );
    },
    completeAssignment: (
      params: Parameters<typeof actual.completeAssignment>[0],
      constraints: Parameters<typeof actual.completeAssignment>[1],
      allowedValues: readonly (readonly boolean[])[],
      partial: TestCase,
      budget?: SolveBudget,
      parameterOrder?: SolveParameterOrder,
      stack?: SolveStack,
    ) => {
      if (parameterOrder === undefined) {
        hooks.callsWithoutOrder += 1;
      } else {
        hooks.callsWithOrder += 1;
      }
      if (stack === undefined) {
        hooks.callsWithoutStack += 1;
      } else {
        hooks.callsWithStack += 1;
        hooks.stackRefs.push(stack);
      }
      // A negative-phase mask pins parameter 0 to a single value; the positive
      // phase leaves every valid value of it available.
      const fixed = allowedValues[0]?.filter(Boolean).length === 1;
      if (fixed && hooks.exhaustOnFixedValue >= 0 && allowedValues[0][hooks.exhaustOnFixedValue]) {
        if (budget !== undefined) {
          budget.remaining = 0;
          budget.exceeded = true;
        }
        return null;
      }
      return actual.completeAssignment(
        params,
        constraints,
        allowedValues,
        partial,
        budget,
        parameterOrder,
        stack,
      );
    },
    completeValidAssignment: (
      params: Parameters<typeof actual.completeValidAssignment>[0],
      constraints: Parameters<typeof actual.completeValidAssignment>[1],
      partial: TestCase,
      budget?: SolveBudget,
      parameterOrder?: SolveParameterOrder,
      stack?: SolveStack,
    ) => {
      hooks.validCalls += 1;
      if (parameterOrder === undefined) {
        hooks.callsWithoutOrder += 1;
      } else {
        hooks.callsWithOrder += 1;
      }
      if (stack === undefined) {
        hooks.callsWithoutStack += 1;
      } else {
        hooks.callsWithStack += 1;
        hooks.stackRefs.push(stack);
      }
      return actual.completeValidAssignment(
        params,
        constraints,
        partial,
        budget,
        parameterOrder,
        stack,
      );
    },
  };
});

beforeEach(() => {
  hooks.reset();
});

/** Model with one invalid value on the first parameter. */
function negativeModel(constraintExpressions: string[] = []): GenerateOptions {
  return createGenerateOptions({
    parameters: [
      { name: 'A', values: ['a0', 'a1', 'bad'], invalid: [false, false, true] },
      { name: 'B', values: ['b0', 'b1', 'b2'] },
      { name: 'C', values: ['c0', 'c1', 'c2'] },
      { name: 'D', values: ['d0', 'd1', 'd2'] },
    ],
    strength: 2,
    seed: 42,
    constraintExpressions,
  });
}

describe('generate solver interaction', () => {
  it('hands the solver a prebuilt parameter order on every feasibility call', () => {
    // The solver rebuilds the order itself whenever the caller omits it, so a
    // loop whose mask never changes must pass one in. Counting the calls that
    // left it out is the cheapest way to see a per-witness rebuild come back.
    const result = generate(negativeModel());

    expect(result.error.code).toBe(ErrorCode.Ok);
    // Enough witnesses that a per-witness rebuild would be plainly visible.
    expect(result.negativeTests.length).toBeGreaterThan(5);
    expect(hooks.callsWithOrder).toBeGreaterThanOrEqual(result.negativeTests.length);
    expect(hooks.callsWithoutOrder).toBe(0);
    expect(hooks.callsWithStack).toBeGreaterThanOrEqual(result.negativeTests.length);
    expect(hooks.callsWithoutStack).toBe(0);
    expect(new Set(hooks.stackRefs).size).toBe(1);
  });

  it('passes reusable order and stack through the suggestion loop', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'a1'] },
          { name: 'B', values: ['b0', 'b1'] },
          { name: 'C', values: ['c0', 'c1'] },
        ],
        strength: 2,
        maxTests: 1,
      }),
    );

    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.uncovered.length).toBeGreaterThan(0);
    expect(result.suggestions.length).toBeGreaterThan(0);
    expect(hooks.validCalls).toBeGreaterThan(0);
    expect(hooks.callsWithoutOrder).toBe(0);
    expect(hooks.callsWithoutStack).toBe(0);
    expect(new Set(hooks.stackRefs).size).toBe(1);
  });

  it('leaves negative coverage unset when a witness exhausts the search budget', () => {
    hooks.exhaustOnFixedValue = 2;

    const result = generate(negativeModel());

    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.error.message).toBe('Constraint search budget exceeded');
    expect(result.error.detail).toBe(
      'A negative coverage witness could not be found within the search budget',
    );
    // The pass stopped before it could work out omitted tuples and the ratio, so
    // publishing its counters would describe a universe never fully classified.
    expect(result.negativeCoverage).toBeUndefined();
  });

  it('reports an unclassifiable target universe apart from a missing witness', () => {
    // With constraints in play the negative pass classifies its targets before
    // looking for witnesses, so the same forced exhaustion surfaces as the
    // exclusion failure instead - the two must not collapse into one message.
    hooks.exhaustOnFixedValue = 2;

    const result = generate(negativeModel(['IF A = "a0" THEN B != "b0"']));

    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.error.message).toBe('Constraint search budget exceeded');
    expect(result.error.detail).toBe(
      'Negative coverage targets could not be classified within the search budget',
    );
    expect(result.negativeCoverage).toBeUndefined();
  });
});
