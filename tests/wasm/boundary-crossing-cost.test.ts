import { beforeAll, describe, expect, it } from 'vitest';

/// What one call costs at the JavaScript boundary, counted rather than timed.
///
/// Every other check in this tree reads text — a source, a message, a shape —
/// and none of them can see a cost. A wrapper that asks the same question twice
/// reads correctly, returns the same answers, and passes all of them, while
/// paying a second crossing out of WebAssembly for every cell of every row. The
/// hot path of this binding is a per-cell path, so that is the regression this
/// surface is exposed to, and it is invisible to any amount of reading.
///
/// The count comes from wrapping the imports the glue hands to
/// `WebAssembly.instantiate` before the module is made: every call out of
/// WebAssembly into JavaScript passes through one of them, so counting them
/// counts crossings exactly. It needs no clock and it is a pure function of the
/// input, which is what lets it be asserted rather than eyeballed.
///
/// Two things make the numbers below properties rather than magic constants.
/// They are differences between two fixtures, so everything a call pays once
/// cancels; and the fixtures differ only in how many cells a row carries — same
/// model, same row count, same result — so what remains is the per-cell cost
/// and nothing else. A figure measured that way is stable against every change
/// that does not touch what a cell costs, and it is checked at more than one
/// size so that it reads as a rate rather than as a total.

interface RawResult {
  error?: true;
  code?: number;
  coverageRatio?: number;
}

interface RawModule {
  analyzeCoverage(
    parameters: unknown,
    tests: unknown,
    strength: unknown,
    constraints: unknown,
  ): RawResult;
  generate(input: unknown): RawResult;
}

/// Calls out of WebAssembly since the last reset, by import.
const byImport = new Map<string, number>();
/// The source of each import, which is how one is recognised without depending
/// on its name: the linker minifies those.
const sourceOf = new Map<string, string>();
let counting = false;

/// Wrap every import in @p imports so that calling it is counted.
function instrument(imports: WebAssembly.Imports): void {
  for (const namespace of Object.keys(imports)) {
    const table = imports[namespace];
    if (typeof table !== 'object' || table === null) {
      continue;
    }
    for (const name of Object.keys(table)) {
      const original = (table as Record<string, unknown>)[name];
      if (typeof original !== 'function') {
        continue;
      }
      sourceOf.set(name, String(original).replace(/\s+/g, ' '));
      (table as Record<string, unknown>)[name] = (...args: unknown[]): unknown => {
        if (counting) {
          byImport.set(name, (byImport.get(name) ?? 0) + 1);
        }
        return (original as (...a: unknown[]) => unknown)(...args);
      };
    }
  }
}

interface Crossings {
  /// Every call out of WebAssembly.
  total: number;
  /// Those made by this binding's own guarded reads, which are the only route
  /// by which a caller-supplied property is read.
  guarded: number;
}

function crossings(run: () => void): Crossings {
  byImport.clear();
  counting = true;
  try {
    run();
  } finally {
    counting = false;
  }
  let total = 0;
  let guarded = 0;
  for (const [name, calls] of byImport) {
    total += calls;
    if (/\bcoverwise_js_/.test(sourceOf.get(name) ?? '')) {
      guarded += calls;
    }
  }
  return { total, guarded };
}

const WIDTH = 8;
const ROWS = 200;

const parameters = () =>
  Array.from({ length: WIDTH }, (_, i) => ({ name: `p${i}`, values: ['a', 'b'] }));

/// A row carrying the model's own keys plus @p extra keys it does not declare.
///
/// Extra keys are read exactly as declared ones are — the binding reads every
/// own key of a row before it knows which the model has — so varying only this
/// changes the number of cells and leaves the model, the row count and the
/// result untouched. That is what makes the difference below a per-cell figure
/// rather than a per-row one with a share of the row's own cost folded in.
const suite = (extra: number) => {
  const row: Record<string, string> = {};
  for (let i = 0; i < WIDTH; i += 1) {
    row[`p${i}`] = 'a';
  }
  for (let i = 0; i < extra; i += 1) {
    row[`x${i}`] = 'a';
  }
  return Array.from({ length: ROWS }, () => row);
};

/// The crossings a step from @p from to @p to extra keys adds, per added cell.
function perCell(raw: RawModule, from: number, to: number): Crossings {
  const model = parameters();
  const before = crossings(() => raw.analyzeCoverage(model, suite(from), 2, []));
  const after = crossings(() => raw.analyzeCoverage(model, suite(to), 2, []));
  const cells = ROWS * (to - from);
  return {
    total: (after.total - before.total) / cells,
    guarded: (after.guarded - before.guarded) / cells,
  };
}

/// Three steps, so a figure that holds is a rate and not a coincidence.
const STEPS: Array<[number, number]> = [
  [0, 4],
  [4, 8],
  [8, 16],
];

let raw: RawModule;

describe('what a cell of caller input costs at the boundary', () => {
  beforeAll(async () => {
    const real = WebAssembly.instantiate;
    WebAssembly.instantiate = ((source: never, imports: WebAssembly.Imports) => {
      instrument(imports);
      return (real as (s: never, i: WebAssembly.Imports) => unknown)(source, imports);
    }) as unknown as typeof WebAssembly.instantiate;
    try {
      // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
      const createModule = await import('../coverwise.js');
      raw = (await createModule.default()) as RawModule;
    } finally {
      WebAssembly.instantiate = real;
    }
    // Anything resolved once for the life of the module is resolved here, so
    // the counts below see per-call work only.
    raw.analyzeCoverage(parameters(), suite(0), 2, []);
  });

  it('counts the calls the module actually makes', () => {
    // Guards the instrument: a wrapper that missed the import table would make
    // every figure below zero, and zero satisfies every one of them.
    const measured = crossings(() => raw.analyzeCoverage(parameters(), suite(0), 2, []));
    expect(measured.total).toBeGreaterThan(0);
    expect(measured.guarded).toBeGreaterThan(0);
    expect(measured.guarded).toBeLessThan(measured.total);
  });

  it('reads a cell with exactly one crossing', () => {
    // One read, one call out of WebAssembly. This is the figure a wrapper that
    // asks the same question twice doubles, and the one no amount of reading
    // the source can check: the second read returns the same answer.
    for (const [from, to] of STEPS) {
      expect({ step: `${from}->${to}`, guarded: perCell(raw, from, to).guarded }).toEqual({
        step: `${from}->${to}`,
        guarded: 1,
      });
    }
  });

  it('charges the same for a cell however many the row carries', () => {
    // The whole boundary cost this time, not just the reads: a cell that grew
    // more expensive as rows grew wider would be a cost that scales worse than
    // the input, which is the failure the per-cell rate above cannot see. The
    // rate is compared against itself rather than against a number, so a change
    // that makes every cell cheaper or dearer is not a failure here.
    const rates = STEPS.map(([from, to]) => perCell(raw, from, to).total);
    expect(rates).toEqual(new Array(rates.length).fill(rates[0]));
  });
});

/// The shape questions the binding asks are answered by JavaScript globals, and
/// a name resolved against the global object on every call is a cost paid once
/// per row and once per declared value. Counting the resolutions is what tells
/// the two apart, and it is the same kind of instrument as the one above: a
/// count, not a clock.
describe('what a suite costs in resolutions of a JS global', () => {
  const realArray = Array;

  /// How many times @p run reads `Array` off the global object.
  function readsOfArrayGlobal(run: () => void): number {
    let reads = 0;
    Object.defineProperty(globalThis, 'Array', {
      configurable: true,
      get: () => {
        reads += 1;
        return realArray;
      },
    });
    try {
      run();
    } finally {
      Object.defineProperty(globalThis, 'Array', {
        configurable: true,
        writable: true,
        value: realArray,
      });
    }
    return reads;
  }

  const filled = () =>
    Object.fromEntries(realArray.from({ length: WIDTH }, (_, i) => [`p${i}`, 'a']));

  /// Values in object form, which is the shape that makes the value loop ask
  /// whether each one is an array.
  const objectValues = (count: number) => [
    { name: 'n', values: realArray.from({ length: count }, (_, i) => ({ value: `v${i}` })) },
    { name: 'm', values: ['x', 'y'] },
  ];

  beforeAll(() => {
    raw.analyzeCoverage(parameters(), [filled()], 2, []);
    raw.generate({ parameters: objectValues(4), strength: 1 });
  });

  it('does not resolve one per row', () => {
    const model = parameters();
    const few = readsOfArrayGlobal(() => {
      raw.analyzeCoverage(model, new realArray(10).fill(filled()), 2, []);
    });
    const many = readsOfArrayGlobal(() => {
      raw.analyzeCoverage(model, new realArray(1000).fill(filled()), 2, []);
    });
    expect(many).toBe(few);
  });

  it('does not resolve one per declared value', () => {
    const few = readsOfArrayGlobal(() => {
      raw.generate({ parameters: objectValues(10), strength: 1 });
    });
    const many = readsOfArrayGlobal(() => {
      raw.generate({ parameters: objectValues(1000), strength: 1 });
    });
    expect(many).toBe(few);
  });
});
