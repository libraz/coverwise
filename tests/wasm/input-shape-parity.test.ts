import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from '../../js/index.js';
import * as pure from '../../js/pure/index.js';
import type { GenerateInput, Parameter, TestCase } from '../../js/types.js';
import { CoverwiseError } from '../../js/types.js';

/// Caller data is read one way on both npm entries: a named field is an own key,
/// a row's cells are its own enumerable keys, a hole in an array is an absent
/// element, and a string with an unpaired surrogate is refused wherever it
/// appears. Each case runs on both entries and requires the same outcome.

beforeAll(async () => {
  await wasm.init();
});

const model = (): Parameter[] => [
  { name: 'n', values: ['a', 'b'] },
  { name: 'm', values: ['x', 'y'] },
];

type Outcome =
  | { rejected: false; value: unknown }
  | { rejected: true; code: string; message: string; detail: string | undefined };

function outcome(call: () => unknown): Outcome {
  try {
    return { rejected: false, value: call() };
  } catch (thrown) {
    // A foreign throw fails the comparison below rather than being reported.
    if (!(thrown instanceof CoverwiseError)) {
      throw thrown;
    }
    return { rejected: true, code: thrown.code, message: thrown.message, detail: thrown.detail };
  }
}

/** The same call on both entries, requiring one outcome from them. */
function onBothEntries(call: (api: typeof wasm | typeof pure) => unknown): Outcome {
  const fromWasm = outcome(() => call(wasm));
  const fromPure = outcome(() => call(pure));
  expect(fromPure).toEqual(fromWasm);
  return fromWasm;
}

function expectRejected(result: Outcome): { code: string; message: string } {
  expect(result.rejected).toBe(true);
  return result as { code: string; message: string };
}

/** A row whose `n` cell is defined with the given enumerability. */
function rowWithCell(value: unknown, enumerable: boolean): TestCase {
  const built = { m: 'x' } as TestCase;
  Object.defineProperty(built, 'n', { value, enumerable, configurable: true, writable: true });
  return built;
}

describe('a row cell that is not an enumerable key', () => {
  it('is read as absent on both entries, whatever it holds', () => {
    for (const value of ['a', null]) {
      const result = onBothEntries((api) =>
        api.analyzeCoverage(model(), [rowWithCell(value, false), { n: 'b', m: 'y' }], 1),
      );
      expect(result.rejected, String(value)).toBe(false);
      const report = (result as { value: { invalidTests: Array<{ testIndex: number }> } }).value;
      expect(report.invalidTests.map((invalidRow) => invalidRow.testIndex)).toEqual([0]);
    }
  });

  it('is refused as a missing parameter when it stands in a seed', () => {
    const result = onBothEntries((api) =>
      api.generate({ parameters: model(), seeds: [rowWithCell('a', false)] }),
    );
    const error = expectRejected(result);
    expect(error.code).toBe('INVALID_INPUT');
    expect(error.message).toContain("missing parameter 'n'");
  });
});

describe('a row cell that is not a scalar', () => {
  it('is refused with INVALID_INPUT when it is null', () => {
    const result = onBothEntries((api) =>
      api.analyzeCoverage(model(), [rowWithCell(null, true)], 1),
    );
    expect(expectRejected(result).code).toBe('INVALID_INPUT');
  });
});

describe('a hole in an array field', () => {
  const sparse = <T>(length: number, ...present: Array<[number, T]>): T[] => {
    const built = new Array<T>(length);
    for (const [index, value] of present) {
      built[index] = value;
    }
    return built;
  };

  const cases: Array<{ field: string; input: () => GenerateInput }> = [
    {
      field: 'aliases',
      input: () => ({
        parameters: [
          { name: 'n', values: [{ value: 'a', aliases: sparse<string>(2, [0, 'x']) }, 'b'] },
          { name: 'm', values: ['x', 'y'] },
        ],
      }),
    },
    {
      field: 'constraints',
      input: () => ({ parameters: model(), constraints: sparse<string>(2, [0, 'n = "a"']) }),
    },
    {
      field: 'subModels',
      input: () => ({
        parameters: model(),
        subModels: [{ parameters: sparse<string>(2, [0, 'n']), strength: 1 }],
      }),
    },
    {
      field: 'values',
      input: () => ({
        parameters: [{ name: 'n', values: sparse<string>(2, [0, 'a']) }, ...model().slice(1)],
      }),
    },
  ];

  for (const { field, input } of cases) {
    it(`is refused as invalid ${field} on both entries`, () => {
      const result = onBothEntries((api) => api.generate(input()));
      expect(expectRejected(result).code).toBe('INVALID_INPUT');
    });
  }
});

describe('a row with many keys the model does not name', () => {
  it('is accepted on both entries', () => {
    const wide: TestCase = { n: 'a', m: 'x' };
    for (let i = 0; i < 2000; ++i) {
      wide[`extra${i}`] = i;
    }
    const result = onBothEntries((api) => api.analyzeCoverage(model(), [wide], 1));
    expect(result.rejected).toBe(false);
  });
});

describe('a string with an unpaired surrogate', () => {
  const lone = 'a\ud800';
  const loneLow = '\udc00b';

  const cases: Array<{ field: string; call: (api: typeof wasm | typeof pure) => unknown }> = [
    {
      field: 'a parameter name',
      call: (api) =>
        api.generate({ parameters: [{ name: `os${lone}`, values: ['a', 'b'] }, ...model()] }),
    },
    {
      field: 'a parameter value',
      call: (api) => api.generate({ parameters: [{ name: 'k', values: [lone, 'b'] }, ...model()] }),
    },
    {
      field: 'a value in object form',
      call: (api) =>
        api.generate({ parameters: [{ name: 'k', values: [{ value: loneLow }] }, ...model()] }),
    },
    {
      field: 'an alias',
      call: (api) =>
        api.generate({
          parameters: [{ name: 'k', values: [{ value: 'a', aliases: [lone] }] }, ...model()],
        }),
    },
    {
      field: 'an equivalence class',
      call: (api) =>
        api.generate({
          parameters: [{ name: 'k', values: [{ value: 'a', class: lone }] }, ...model()],
        }),
    },
    {
      field: 'a constraint',
      call: (api) => api.generate({ parameters: model(), constraints: [`n = "${lone}"`] }),
    },
    {
      field: 'a sub-model parameter name',
      call: (api) =>
        api.generate({ parameters: model(), subModels: [{ parameters: [lone], strength: 1 }] }),
    },
    {
      field: 'a weight key',
      call: (api) => api.generate({ parameters: model(), weights: { n: { [lone]: 2 } } }),
    },
    {
      field: 'a seed cell',
      call: (api) => api.generate({ parameters: model(), seeds: [{ n: lone, m: 'x' }] }),
    },
    {
      field: 'a tests cell',
      call: (api) => api.analyzeCoverage(model(), [{ n: lone, m: 'x' }], 1),
    },
    {
      field: 'an existing cell',
      call: (api) => api.extendTests([{ n: lone, m: 'x' }], { parameters: model() }),
    },
    {
      field: 'a row key naming no parameter',
      call: (api) => api.analyzeCoverage(model(), [{ n: 'a', m: 'x', [loneLow]: 'z' }], 1),
    },
  ];

  for (const { field, call } of cases) {
    it(`is refused in ${field} with one code and message on both entries`, () => {
      const error = expectRejected(onBothEntries(call));
      expect(error.code).toBe('INVALID_INPUT');
      expect(error.message).toContain('unpaired surrogate');
    });
  }

  it('is accepted when the surrogates are paired', () => {
    const paired = '😀';
    const result = onBothEntries((api) =>
      api.generate({ parameters: [{ name: 'k', values: [paired, 'b'] }, ...model()] }),
    );
    expect(result.rejected).toBe(false);
  });
});
