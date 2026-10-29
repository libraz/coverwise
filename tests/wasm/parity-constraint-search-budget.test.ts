import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from '../../js/index.js';
import { CoverwiseError } from '../../js/index.js';
import * as pure from '../../js/pure/index.js';
import type { GenerateInput, Parameter } from '../../js/types.js';

/// The constraint search is node-bounded, and a search that ran out of nodes
/// has to be told apart from one that finished and found nothing. Every exit
/// that spends the budget answers with the same code, message and detail on the
/// WASM and the pure surface, and none of them hands back a suite whose coverage
/// accounting describes a tuple universe that was never finished being
/// classified.

const HOLES = 10;
const PIGEONS = 11;
const BUDGET_MESSAGE = 'Constraint search budget exceeded';

const holes = Array.from({ length: HOLES }, (_, i) => `h${i}`);

/// Eleven parameters that must all differ while holding ten values each. The
/// model has no solution, and proving that costs more nodes than the budget
/// allows. `guard` makes every constraint conditional on a value of `P0`.
function pigeonholeModel(guard?: string): GenerateInput {
  const parameters: Parameter[] = [];
  if (guard !== undefined) {
    parameters.push({ name: 'P0', values: [...holes, { value: guard, invalid: true }] });
  }
  for (let i = 1; i <= PIGEONS; i++) {
    parameters.push({ name: `P${i}`, values: holes });
  }
  const prefix = guard === undefined ? '' : `IF P0 = "${guard}" THEN `;
  const constraints: string[] = [];
  for (let left = 1; left <= PIGEONS; left++) {
    for (let right = left + 1; right <= PIGEONS; right++) {
      constraints.push(`${prefix}P${left} != P${right}`);
    }
  }
  return { parameters, constraints, strength: 2, seed: 42 };
}

/// Either the error a surface refused with or the value it returned.
type Outcome =
  | { kind: 'error'; code: string; message: string; detail: string | undefined }
  | { kind: 'value'; value: unknown };

function outcomeOf(fn: () => unknown): Outcome {
  try {
    return { kind: 'value', value: fn() };
  } catch (error) {
    expect(error).toBeInstanceOf(CoverwiseError);
    const { code, message, detail } = error as CoverwiseError;
    return { kind: 'error', code, message, detail };
  }
}

/// The ways a model reaches the budget, each through a different exit of the
/// generator or the validator.
const entries = [
  {
    name: 'generate on an unsolvable model',
    detail: 'The constraint model is too complex to solve within the search budget',
    run: (api: typeof wasm | typeof pure) => api.generate(pigeonholeModel()),
  },
  {
    name: 'generate on a model whose negative targets cannot be classified',
    detail: 'Negative coverage targets could not be classified within the search budget',
    run: (api: typeof wasm | typeof pure) => api.generate(pigeonholeModel('bad')),
  },
  {
    name: 'extendTests on an unsolvable model',
    detail: 'The constraint model is too complex to solve within the search budget',
    run: (api: typeof wasm | typeof pure) => api.extendTests([], pigeonholeModel()),
  },
  {
    name: 'analyzeCoverage on an unsolvable model',
    detail: 'The constraint model is too complex to solve within the search budget',
    run: (api: typeof wasm | typeof pure) => {
      const { parameters, constraints } = pigeonholeModel();
      return api.analyzeCoverage(parameters, [], 2, constraints);
    },
  },
] as const;

beforeAll(async () => {
  await wasm.init();
});

describe('constraint search budget parity', () => {
  for (const { name, detail, run } of entries) {
    it(`${name}: both surfaces refuse with the budget error`, { timeout: 120_000 }, () => {
      const fromWasm = outcomeOf(() => run(wasm));
      const fromPure = outcomeOf(() => run(pure));

      expect(fromWasm).toEqual({
        kind: 'error',
        code: 'CONSTRAINT_ERROR',
        message: BUDGET_MESSAGE,
        detail,
      });
      expect(fromPure).toEqual(fromWasm);
    });
  }
});
