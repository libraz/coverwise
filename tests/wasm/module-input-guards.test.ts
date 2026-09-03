import { beforeAll, describe, expect, it } from 'vitest';
import { MAX_AGGREGATE_STRING_BYTES, MAX_TESTS } from '../../src/ts/model/limits.js';

/// The shape checks in js/validation.ts run before either package entry point
/// reaches an engine, so they hide what the binding would have done on its own.
/// An embedder calling the compiled module directly gets no such cover: these
/// call it that way, so what is asserted here is the binding's own reading of
/// the input and nothing else.
///
/// Two properties hold across every case. A malformed input comes back as
/// `{ error: true, code: 3 }` — a returned value, never a thrown JS exception,
/// because a raw TypeError crossing the C++ boundary would escape the caller's
/// error handling entirely. And a rejected input yields no test suite at all:
/// the binding never repairs a value it could not read.

interface RawResult {
  error?: true;
  code?: number;
  message?: string;
  strength?: number;
  tests?: Array<Record<string, string>>;
  coverageRatio?: number;
}

interface RawModule {
  generate(input: unknown): RawResult;
  analyzeCoverage(
    parameters: unknown,
    tests: unknown,
    strength: unknown,
    constraints: unknown,
  ): RawResult;
  extendTests(existing: unknown, input: unknown): RawResult;
  estimateModel(input: unknown): RawResult;
}

const INVALID_INPUT = 3;

/// A two-parameter model that generates cleanly, so anything a case observes is
/// the field it changed.
const model = () => [
  { name: 'n', values: ['a', 'b'] },
  { name: 'm', values: ['x', 'y'] },
];

let raw: RawModule;

/// Calls the module and fails if the call throws rather than returning.
function call(invoke: () => RawResult): RawResult {
  let result: RawResult | undefined;
  expect(() => {
    result = invoke();
  }).not.toThrow();
  return result as RawResult;
}

describe('the compiled module reading input on its own', () => {
  beforeAll(async () => {
    // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
    const createModule = await import('../coverwise.js');
    raw = (await createModule.default()) as RawModule;
  });

  describe('parameter values', () => {
    const malformed: Array<{ label: string; element: unknown }> = [
      { label: 'an object with no value member', element: {} },
      { label: 'an object whose value is null', element: { value: null } },
      { label: 'an object whose value is an object', element: { value: {} } },
      { label: 'an object whose value is an array', element: { value: [1] } },
      { label: 'a bare null', element: null },
      { label: 'a bare undefined', element: undefined },
      { label: 'a nested array', element: [1, 2] },
    ];

    for (const { label, element } of malformed) {
      it(`rejects ${label} instead of inventing a value for it`, () => {
        const result = call(() =>
          raw.generate({
            parameters: [{ name: 'n', values: [element, 'ok'] }, model()[1]],
          }),
        );

        expect(result.error).toBe(true);
        expect(result.code).toBe(INVALID_INPUT);
        expect(result.message).toBe('Invalid value at n[0]: expected string, number, or boolean.');
        // No suite comes back, so no row can carry a value the caller never
        // wrote — "undefined", "null" and "[object Object]" included.
        expect(result.tests).toBeUndefined();
      });
    }

    it('keeps every value the caller did write', () => {
      const result = call(() =>
        raw.generate({
          parameters: [{ name: 'n', values: [{ value: 'a' }, 'b', 3, true] }, model()[1]],
        }),
      );
      expect(result.error).toBeUndefined();
      const written = new Set(['a', 'b', '3', 'true']);
      for (const test of result.tests as Array<Record<string, string>>) {
        expect(written.has(test.n)).toBe(true);
      }
    });
  });

  describe('array-typed members of a value', () => {
    it('rejects a string where an alias array belongs', () => {
      const result = call(() =>
        raw.generate({
          parameters: [
            { name: 'n', values: [{ value: 'chromium', aliases: 'chrome' }, 'b'] },
            model()[1],
          ],
        }),
      );

      expect(result.error).toBe(true);
      expect(result.code).toBe(INVALID_INPUT);
      // A string carries a length and is indexable, so without the guard the
      // eight characters of an unwrapped name become eight aliases.
      expect(result.message).toBe('Aliases at n[0] must be non-empty strings.');
      expect(result.tests).toBeUndefined();
    });

    for (const { label, aliases } of [
      { label: 'a non-string alias', aliases: ['chrome', 7] },
      { label: 'an empty alias', aliases: ['chrome', ''] },
    ]) {
      it(`rejects ${label}`, () => {
        const result = call(() =>
          raw.generate({
            parameters: [{ name: 'n', values: [{ value: 'chromium', aliases }, 'b'] }, model()[1]],
          }),
        );
        expect(result.error).toBe(true);
        expect(result.code).toBe(INVALID_INPUT);
        expect(result.message).toBe('Aliases at n[0] must be non-empty strings.');
      });
    }

    it('rejects a string where a sub-model parameter array belongs', () => {
      const result = call(() =>
        raw.generate({
          parameters: model(),
          subModels: [{ parameters: 'n', strength: 2 }],
        }),
      );
      expect(result.error).toBe(true);
      expect(result.code).toBe(INVALID_INPUT);
      expect(result.message).toBe('Invalid subModels[0].');
    });

    it('rejects a non-string constraint expression', () => {
      const result = call(() => raw.generate({ parameters: model(), constraints: [42] }));
      expect(result.error).toBe(true);
      expect(result.code).toBe(INVALID_INPUT);
      expect(result.message).toBe('Invalid constraints: must be an array of strings.');
    });
  });

  describe('optional fields holding undefined or null', () => {
    for (const { label, absent } of [
      { label: 'undefined', absent: undefined },
      { label: 'null', absent: null },
    ]) {
      it(`applies the documented default when every optional field is ${label}`, () => {
        const result = call(() =>
          raw.generate({
            parameters: model(),
            strength: absent,
            seed: absent,
            maxTests: absent,
            constraints: absent,
            seeds: absent,
            weights: absent,
            subModels: absent,
          }),
        );
        expect(result.error).toBeUndefined();
        expect(result.strength).toBe(2);
      });

      it(`applies the documented default when a value's members are ${label}`, () => {
        const result = call(() =>
          raw.generate({
            parameters: [
              {
                name: 'n',
                values: [{ value: 'a', invalid: absent, aliases: absent, class: absent }, 'b'],
              },
              model()[1],
            ],
          }),
        );
        expect(result.error).toBeUndefined();
        const values = new Set((result.tests as Array<Record<string, string>>).map((t) => t.n));
        expect([...values].sort()).toEqual(['a', 'b']);
      });

      it(`applies the documented default when a boundary step is ${label}`, () => {
        const result = call(() =>
          raw.generate({
            parameters: [{ name: 'n', values: [], type: 'integer', range: [0, 4], step: absent }],
            strength: 1,
          }),
        );
        expect(result.error).toBeUndefined();
      });
    }
  });

  describe('the documented row ceiling', () => {
    const row = { n: 'a', m: 'x' };

    it('refuses a suite above the ceiling and names the field it read', () => {
      // Every row is the same object: what is being measured is the count.
      const rows = new Array(MAX_TESTS + 1).fill(row);

      const analyzed = call(() => raw.analyzeCoverage(model(), rows, 2, []));
      expect(analyzed.error).toBe(true);
      expect(analyzed.code).toBe(INVALID_INPUT);
      expect(analyzed.message).toBe(`Invalid tests: maximum is ${MAX_TESTS} rows.`);

      const extended = call(() => raw.extendTests(rows, { parameters: model() }));
      expect(extended.error).toBe(true);
      expect(extended.message).toBe(`Invalid existing: maximum is ${MAX_TESTS} rows.`);

      const generated = call(() => raw.generate({ parameters: model(), seeds: rows }));
      expect(generated.error).toBe(true);
      expect(generated.message).toBe(`Invalid seeds: maximum is ${MAX_TESTS} rows.`);
    });

    it('accepts a suite exactly at the ceiling', { timeout: 120_000 }, () => {
      const rows = new Array(MAX_TESTS).fill(row);
      const analyzed = call(() => raw.analyzeCoverage(model(), rows, 2, []));
      expect(analyzed.error).toBeUndefined();
      expect(analyzed.coverageRatio).toBeGreaterThan(0);
    });
  });

  /// A row array is the largest text a call carries, and it is text the engine
  /// never sees: a row arrives as value names and reaches the engine as value
  /// indices, so unless it is counted where it is read it is not counted at
  /// all. The row-count ceiling above does not stand in for this one — it
  /// bounds how many rows may arrive, not how much text they carry.
  describe('the documented byte ceiling on row text', () => {
    const VALUE = 'aaaaa';

    /// A model of @p width parameters, each declaring two values of VALUE's
    /// length, and a row filling every one of them.
    const wideModel = (width: number) =>
      Array.from({ length: width }, (_, i) => ({ name: `p${i}`, values: [VALUE, 'bbbbb'] }));
    const filledRow = (width: number) =>
      Object.fromEntries(Array.from({ length: width }, (_, i) => [`p${i}`, VALUE]));
    const suite = (count: number, width: number) => new Array(count).fill(filledRow(width));

    /// What the budget has left for rows once the model's own strings are
    /// charged: parameter names once each, declared values once each. Stated as
    /// arithmetic rather than as a number, so a case reads as the rule it
    /// checks and a change to the limit does not leave a stale constant behind.
    const rowsAllowed = (width: number): number => {
      const names = Array.from({ length: width }, (_, i) => `p${i}`.length).reduce((a, b) => a + b);
      const declared = width * (VALUE.length + 'bbbbb'.length);
      return Math.floor((MAX_AGGREGATE_STRING_BYTES - names - declared) / (width * VALUE.length));
    };

    for (const width of [10, 100]) {
      it(`accepts what the budget allows at ${width} parameters and refuses one row more`, {
        timeout: 120_000,
      }, () => {
        const allowed = rowsAllowed(width);
        const parameters = wideModel(width);

        expect(
          call(() => raw.analyzeCoverage(parameters, suite(allowed, width), 2, [])).error,
        ).toBeUndefined();

        const over = call(() => raw.analyzeCoverage(parameters, suite(allowed + 1, width), 2, []));
        expect(over.error).toBe(true);
        expect(over.code).toBe(INVALID_INPUT);
      });
    }

    it('bounds every entry that reads rows by the same total', { timeout: 120_000 }, () => {
      const width = 10;
      const parameters = wideModel(width);
      const over = rowsAllowed(width) + 1;

      for (const entry of [
        () => raw.analyzeCoverage(parameters, suite(over, width), 2, []),
        () => raw.extendTests(suite(over, width), { parameters }),
        () => raw.generate({ parameters, seeds: suite(over, width) }),
      ]) {
        const result = call(entry);
        expect(result.error).toBe(true);
        expect(result.code).toBe(INVALID_INPUT);
      }
    });

    it('draws both row arrays of one call from one total', { timeout: 120_000 }, () => {
      // Extend reads `existing` and `seeds`. Two half-sized suites are the same
      // dimension of input as one full-sized one, so splitting a suite across
      // the two must not buy twice the room.
      const width = 10;
      const parameters = wideModel(width);
      const half = Math.floor(rowsAllowed(width) / 2);

      expect(
        call(() => raw.extendTests(suite(half, width), { parameters, seeds: suite(half, width) }))
          .error,
      ).toBeUndefined();

      const split = call(() =>
        raw.extendTests(suite(half + 2, width), { parameters, seeds: suite(half + 2, width) }),
      );
      expect(split.error).toBe(true);
      expect(split.code).toBe(INVALID_INPUT);
    });

    it('charges the caller text and not what the engine renders', { timeout: 120_000 }, () => {
      // Numbers and booleans are supplied as values, not as text, and a row key
      // is a parameter name the model has already been charged for once. A
      // suite of them well past the byte ceiling is therefore still acceptable
      // — what the limit bounds is the text the caller wrote.
      const width = 10;
      const parameters = Array.from({ length: width }, (_, i) => ({
        name: `p${i}`,
        values: [0, 1],
      }));
      const numericRow = Object.fromEntries(Array.from({ length: width }, (_, i) => [`p${i}`, 0]));
      const rows = new Array(rowsAllowed(width) * 3).fill(numericRow);

      expect(call(() => raw.analyzeCoverage(parameters, rows, 2, [])).error).toBeUndefined();
    });
  });

  /// The ceilings above prove the same number comes out of every surface. They
  /// do not prove the same set of strings goes in: a surface that quietly began
  /// charging row keys again would move a ceiling by an amount no one would
  /// recognise as "row keys". So each kind the contract distinguishes gets its
  /// own case, on a fixture calibrated to sit one instance short of the limit —
  /// charged kinds must push it over, and the kinds this surface is the only
  /// one able to see must not.
  describe('which strings the aggregate budget counts', () => {
    /// One instance, large enough that a single one decides the verdict and
    /// small enough to stay under the per-string cap, so a case that crosses
    /// the aggregate limit is not turned away by the per-string one instead.
    const INSTANCE = 60 * 1024;

    /// A string of exactly @p bytes, tagged so values stay distinct.
    const wide = (bytes: number, tag: string) => tag.padEnd(bytes, 'x');

    const LONG_KEY = wide(INSTANCE, 'k');
    const bigValue = (i: number) => wide(INSTANCE, `v${i}`);

    /// Eight instances of model text: one as a parameter name, seven as
    /// declared values. The numeric and boolean parameters are here so a row
    /// can carry those kinds against a declared value.
    const baseParameters = (): unknown[] => [
      { name: LONG_KEY, values: ['a', 'b'] },
      ...Array.from({ length: 7 }, (_, i) => ({ name: `q${i}`, values: [bigValue(i)] })),
      { name: 'n1', values: [1000000, 1000001] },
      { name: 'b1', values: [true, false] },
    ];

    /// What the gate charges for the model: every parameter name and every
    /// declared value, each once. Rendered as the engine renders them, since
    /// that is the text it charges.
    const modelBytes = (parameters: unknown[]): number =>
      (parameters as Array<{ name: string; values: unknown[] }>).reduce<number>(
        (total, p) =>
          total + p.name.length + p.values.reduce<number>((s, v) => s + String(v).length, 0),
        0,
      );

    const BASE_ROWS = 9;
    const baseRows = () => new Array(BASE_ROWS).fill({ q0: bigValue(0) });

    /// What is left for anything a case adds. Seventeen instances are spent —
    /// eight on the model, nine on rows — so the remainder is small against one
    /// instance and every case below is decided by what it adds.
    const slack = () =>
      MAX_AGGREGATE_STRING_BYTES - modelBytes(baseParameters()) - BASE_ROWS * INSTANCE;

    const analyze = (rows: unknown[]) =>
      call(() => raw.analyzeCoverage(baseParameters(), rows, 2, []));

    /// Refused for the budget specifically. No other rule names that number,
    /// and the wording stays owned by the layer that decides.
    const refusedForBudget = (result: RawResult) => {
      expect(result.error).toBe(true);
      expect(result.code).toBe(INVALID_INPUT);
      expect(result.message).toContain(String(MAX_AGGREGATE_STRING_BYTES));
    };

    it('is calibrated to the byte, and counts unresolved row text once', () => {
      // Every case below reads as evidence only if the fixture really does sit
      // against the limit: without this, a "not charged" case could pass
      // because the fixture had room to spare.
      expect(analyze(baseRows()).error).toBeUndefined();

      // Row text the model cannot resolve is still the caller's own text. At
      // exactly the remainder the input is still acceptable, and one byte more
      // is not — which also fixes the charge at one per string, since charging
      // twice would put the boundary at half.
      const exact = [...baseRows(), { q0: wide(slack(), 'f') }];
      expect(analyze(exact).error).toBeUndefined();

      const over = [...baseRows(), { q0: wide(slack() + 1, 'f') }];
      refusedForBudget(analyze(over));
    });

    it('counts a string row value', () => {
      // One more instance of the kind the base is built from.
      refusedForBudget(analyze([...baseRows(), { q0: bigValue(0) }]));
    });

    it('does not count a row key', () => {
      // A hundred rows keyed by the instance-sized parameter name. Their values
      // are one byte each, so a hundred bytes of the remainder is spent; were
      // the keys charged, this would add a hundred instances and exceed the
      // limit several times over.
      const rows = [...baseRows(), ...new Array(100).fill({ [LONG_KEY]: 'a' })];
      expect(analyze(rows).error).toBeUndefined();
    });

    it('does not count a numeric row value', () => {
      // Enough rows that the text these numbers would render to exceeds the
      // remainder, so accepting them is a statement about numbers rather than
      // about there being room.
      const rendered = String(1000000).length;
      const count = Math.ceil(slack() / rendered) + 1;
      const rows = [...baseRows(), ...new Array(count).fill({ n1: 1000000 })];
      expect(analyze(rows).error).toBeUndefined();
    });

    it('does not count a boolean row value', () => {
      const rendered = String(true).length;
      const count = Math.ceil(slack() / rendered) + 1;
      const rows = [...baseRows(), ...new Array(count).fill({ b1: true })];
      expect(analyze(rows).error).toBeUndefined();
    });
  });

  describe('the field named in a rejection', () => {
    it('names the array the caller actually supplied', () => {
      expect(call(() => raw.generate({ parameters: model(), seeds: 'nope' })).message).toBe(
        'Invalid seeds: must be an array.',
      );
      expect(call(() => raw.extendTests('nope', { parameters: model() })).message).toBe(
        'Invalid existing: must be an array.',
      );
      expect(call(() => raw.analyzeCoverage(model(), 'nope', 2, [])).message).toBe(
        'Invalid tests: must be an array.',
      );
    });

    it('names the row and key of a cell that is not a scalar', () => {
      const result = call(() => raw.analyzeCoverage(model(), [{ n: {} }], 2, []));
      expect(result.error).toBe(true);
      expect(result.code).toBe(INVALID_INPUT);
      expect(result.message).toBe('Invalid tests[0].n: expected string, number, or boolean.');
    });
  });

  describe('arguments that are not objects at all', () => {
    it('reports a structured error rather than a JS exception', () => {
      for (const input of [undefined, null, 'nope', 42, [1, 2]]) {
        for (const result of [
          call(() => raw.generate(input)),
          call(() => raw.estimateModel(input)),
          call(() => raw.extendTests([], input)),
        ]) {
          expect(result.error).toBe(true);
          expect(result.code).toBe(INVALID_INPUT);
          expect(result.tests).toBeUndefined();
        }
      }
    });
  });
});
