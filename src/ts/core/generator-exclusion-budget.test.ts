import { ErrorCode } from '../model/error.js';
import { createGenerateOptions } from '../model/generate-options.js';
import type { TestCase } from '../model/test-case.js';
import type { SolveBudget } from './constraint-solver.js';
import { generate } from './generator.js';

/** Counts the sweep's feasibility searches; every one of them exhausts its budget. */
const hooks = vi.hoisted(() => ({ sweepCalls: 0 }));

vi.mock('./constraint-solver.js', async (importOriginal) => {
  const actual = await importOriginal<typeof import('./constraint-solver.js')>();
  return {
    ...actual,
    completeValidAssignmentInPlace: (
      _params: Parameters<typeof actual.completeValidAssignmentInPlace>[0],
      _constraints: Parameters<typeof actual.completeValidAssignmentInPlace>[1],
      _assignment: TestCase,
      budget?: SolveBudget,
    ) => {
      hooks.sweepCalls += 1;
      if (budget !== undefined) {
        budget.remaining = 0;
        budget.exceeded = true;
      }
      return false;
    },
  };
});

describe('tuple exclusion budget', () => {
  // Once the global sweep has exhausted its budget the run is already an
  // error, so the sub-model sweeps must not spend searches classifying theirs.
  it('stops before the sub-model sweeps once the budget is exhausted', () => {
    const result = generate(
      createGenerateOptions({
        parameters: [
          { name: 'A', values: ['a0', 'a1'] },
          { name: 'B', values: ['b0', 'b1'] },
          { name: 'C', values: ['c0', 'c1'] },
          { name: 'D', values: ['d0', 'd1'] },
        ],
        constraintExpressions: ['IF A = "a0" THEN B != "b0"'],
        subModels: [{ parameterNames: ['A', 'B', 'C'], strength: 3 }],
      }),
    );

    expect(result.error.code).toBe(ErrorCode.ConstraintError);
    expect(result.error.message).toBe('Constraint search budget exceeded');
    // The global sweep stops on its first exhausted search; nothing runs after it.
    expect(hooks.sweepCalls).toBe(1);
  });
});
