/// Cross-surface acceptance: the same JSON is accepted, rejected, or degraded
/// the same way by the WASM-backed and pure-TypeScript entry points.
///
/// The expectations here are written to the same numbers the CLI integration
/// tests assert (tests/integration/cli_test.cpp), so a divergence between the
/// native and JavaScript surfaces shows up as a failure on one side or the
/// other rather than as two suites that quietly disagree.

import { beforeAll, describe, expect, it } from 'vitest';
import { boundaryAcceptanceError } from '../src/ts/model/boundary-rules.js';
import { aggregateBudgetExceeded } from '../src/ts/model/budget.js';
import { MAX_AGGREGATE_STRING_BYTES, MAX_STRING_BYTES } from '../src/ts/model/limits.js';
import * as wasm from './index.js';
import * as pure from './pure/index.js';
import type { CoverwiseError, GenerateInput, Parameter, TestCase } from './types.js';
import { validateParameters as validateParameterShape } from './validation.js';

beforeAll(async () => {
  await wasm.init();
});

const surfaces = [
  { name: 'wasm', api: wasm },
  { name: 'pure', api: pure },
] as const;

/** Run `fn`, returning its value or the error it threw. */
function capture<T>(fn: () => T): T | Error {
  try {
    return fn();
  } catch (error) {
    return error as Error;
  }
}

const twoBinaryParameters: Parameter[] = [
  { name: 'a', values: ['x', 'y'] },
  { name: 'b', values: ['1', '2'] },
];

describe('extend keeps a recorded row the model has drifted away from', () => {
  // A recorded suite drifts from the model it was written against — a value
  // gets renamed, a parameter is added. Filling the gap in the model is what
  // extend is for, so a drifted row is kept exactly as written, reported as
  // excluded from coverage, and left out of the coverage figure.
  // The warning is the caller's only account of why a row was left out, so it
  // has to name what they submitted. `names` is the text they wrote; an absent
  // member has none, and the parameter alone identifies it.
  const drifted: Array<{ label: string; row: TestCase; excluded: string; names?: string }> = [
    {
      label: 'a value the model no longer has',
      row: { a: 'removed', b: '1' },
      excluded: 'a',
      names: 'removed',
    },
    { label: 'a missing parameter', row: { a: 'x' }, excluded: 'b' },
  ];

  for (const { label, row, excluded, names } of drifted) {
    it(`keeps a row with ${label} on every surface`, () => {
      const results = surfaces.map(({ api }) =>
        api.extendTests([row], { parameters: twoBinaryParameters }),
      );
      for (const result of results) {
        const warnings = result.warnings.join('\n');
        expect(result.tests[0]).toEqual(row);
        expect(warnings).toContain(`Existing test 0 preserved but excluded from coverage`);
        expect(warnings).toContain(`parameter ${excluded}`);
        if (names !== undefined) {
          expect(warnings).toContain(names);
        }
        // The unassigned sentinel is an implementation detail of the index
        // vector; a caller who sees it learns nothing about their own input.
        expect(warnings).not.toContain('4294967295');
        expect(result.coverage).toBe(1);
      }
      expect(results[0].tests[0]).toEqual(results[1].tests[0]);
      expect(results[0].warnings).toEqual(results[1].warnings);
    });
  }
});

const aliasedBrowser: Parameter[] = [
  { name: 'browser', values: [{ value: 'Chromium', aliases: ['Chrome'] }, { value: 'Firefox' }] },
  { name: 'os', values: ['win', 'mac'] },
];

describe('extend hands back the existing rows exactly as they were supplied', () => {
  // Rendering a preserved row from its value index substitutes the primary
  // value for the alias the caller wrote, so a suite fed back to a runner no
  // longer matches on the strings that were submitted. Only rows the caller did
  // not supply are free to rotate through the alias list.
  const supplied: Array<{ label: string; row: TestCase }> = [
    { label: 'a primary value', row: { browser: 'Chromium', os: 'win' } },
    { label: 'an alias', row: { browser: 'Chrome', os: 'win' } },
  ];

  for (const { label, row } of supplied) {
    it(`returns a row given by ${label} unchanged on every surface`, () => {
      const results = surfaces.map(({ api }) =>
        api.extendTests([row], { parameters: aliasedBrowser }),
      );
      for (const result of results) {
        expect(result.tests[0]).toEqual(row);
      }
      expect(results[0].tests[0]).toEqual(results[1].tests[0]);
    });
  }
});

describe('analyze reports every kind of row mismatch the same way', () => {
  // Missing key, out-of-domain value: one defect class, one code path. The row
  // is excluded from coverage and reported with its index, and the rest of the
  // suite is still measured.
  const mismatches: Array<{ label: string; tests: TestCase[] }> = [
    { label: 'a row missing a parameter', tests: [{ a: 'x' }, { a: 'y', b: '2' }] },
    {
      label: 'a row with an out-of-domain value',
      tests: [
        { a: 'zzz', b: '1' },
        { a: 'y', b: '2' },
      ],
    },
  ];

  for (const { label, tests } of mismatches) {
    it(`reports ${label} in invalidTests on every surface`, () => {
      const reports = surfaces.map(({ api }) => api.analyzeCoverage(twoBinaryParameters, tests, 2));
      for (const report of reports) {
        expect(report.coverageRatio).toBe(0.25);
        expect(report.invalidTests.map((entry) => entry.testIndex)).toEqual([0]);
      }
      expect(reports[0].invalidTests).toEqual(reports[1].invalidTests);
    });
  }
});

describe('boundary parameters', () => {
  // A boundary parameter may spell out only the values it wants marked invalid
  // and leave the valid ones to the range: supplying them is what the range is
  // for. Judging the declared list instead would call the model valueless.
  it('accepts one whose only declared value is an invalid sentinel', () => {
    const input = {
      parameters: [
        { name: 'age', type: 'integer', range: [0, 10], values: [{ value: 999, invalid: true }] },
        { name: 'mode', values: ['a', 'b'] },
      ],
    } as unknown as GenerateInput;

    const results = surfaces.map(({ api }) => api.generate(input));
    for (const result of results) {
      const ages = new Set(result.tests.map((test) => test.age));
      expect([...ages].sort()).toEqual(['-1', '0', '1', '10', '11', '9']);
      expect(result.negativeTests.map((test) => test.age)).toContain('999');
    }
    expect(results[0].tests).toEqual(results[1].tests);
  });

  it('expands the documented value set for an integer step of 1', () => {
    const input = {
      parameters: [
        { name: 'n', type: 'integer', range: [0, 10], step: 1, values: [] },
        { name: 'm', values: ['a', 'b'] },
      ],
    } as unknown as GenerateInput;

    for (const { api } of surfaces) {
      const values = new Set(api.generate(input).tests.map((test) => test.n));
      expect([...values].sort()).toEqual(['-1', '0', '1', '10', '11', '9']);
    }
  });

  // An endpoint sitting exactly on the exact-integer limit is a model the rules
  // accept. A surface that reserved room of its own for the +/-1 the expansion
  // adds refused a model every other surface generated for.
  it('accepts an integer range reaching the exact-integer limit', () => {
    const input = {
      parameters: [
        { name: 'n', type: 'integer', range: [0, Number.MAX_SAFE_INTEGER], values: [] },
        { name: 'm', values: ['a', 'b'] },
      ],
    } as unknown as GenerateInput;

    const suites = surfaces.map(({ api }) => api.generate(input).tests);
    for (const tests of suites) {
      const values = new Set(tests.map((test) => test.n));
      expect([...values].sort()).toEqual([
        '-1',
        '0',
        '1',
        '9007199254740990',
        '9007199254740991',
        '9007199254740992',
      ]);
    }
    expect(suites[0]).toEqual(suites[1]);
  });

  // Where the two surfaces can actually disagree is the model layer: the shape
  // check in js/validation.ts runs first for both of them, so an input it turns
  // away never reaches the code that decides anything. Every entry below is one
  // it lets through and one model-layer rule refuses. The expected text is
  // quoted from the rule module rather than spelled out here, so a wording that
  // is changed for one surface fails against both.
  const modelLayerRejections: Array<{
    label: string;
    parameter: Record<string, unknown>;
    message: string;
  }> = [
    {
      label: 'a range given high-to-low',
      parameter: { type: 'integer', range: [5, 1] },
      message: boundaryAcceptanceError.range('n'),
    },
    {
      label: 'a range endpoint that is not a number',
      parameter: { type: 'integer', range: [Number.NaN, 1] },
      message: boundaryAcceptanceError.range('n'),
    },
    {
      label: 'an unbounded range endpoint',
      parameter: { type: 'float', range: [0, Number.POSITIVE_INFINITY] },
      message: boundaryAcceptanceError.range('n'),
    },
    {
      label: 'a float step of zero',
      parameter: { type: 'float', range: [0, 10], step: 0 },
      message: boundaryAcceptanceError.floatStep('n'),
    },
    {
      label: 'a negative float step',
      parameter: { type: 'float', range: [0, 10], step: -1 },
      message: boundaryAcceptanceError.floatStep('n'),
    },
    {
      label: 'a float step that is not a number',
      parameter: { type: 'float', range: [0, 10], step: Number.NaN },
      message: boundaryAcceptanceError.expansion('n'),
    },
    {
      label: 'an unbounded float step',
      parameter: { type: 'float', range: [0, 10], step: Number.POSITIVE_INFINITY },
      message: boundaryAcceptanceError.expansion('n'),
    },
    {
      label: 'an integer step other than one',
      parameter: { type: 'integer', range: [0, 10], step: 5 },
      message: boundaryAcceptanceError.integerStep('n'),
    },
    {
      label: 'an integer step of zero',
      parameter: { type: 'integer', range: [0, 10], step: 0 },
      message: boundaryAcceptanceError.integerStep('n'),
    },
    {
      label: 'a fractional integer endpoint',
      parameter: { type: 'integer', range: [0, 2.5] },
      message: boundaryAcceptanceError.integerEndpoints('n'),
    },
    {
      label: 'an integer endpoint past the exact-integer limit',
      parameter: { type: 'integer', range: [0, Number.MAX_SAFE_INTEGER + 2] },
      message: boundaryAcceptanceError.integerEndpoints('n'),
    },
    {
      label: 'two declared values with one numeric identity',
      parameter: { type: 'integer', range: [0, 10], values: ['1', '1.0'] },
      message: boundaryAcceptanceError.duplicateIdentities('n'),
    },
    {
      label: 'a declared value no double can hold',
      parameter: { type: 'integer', range: [0, 10], values: ['1e999'] },
      message: boundaryAcceptanceError.nonFiniteValue('n', '1e999'),
    },
    {
      label: 'a fractional declared value on an integer parameter',
      parameter: { type: 'integer', range: [0, 10], values: ['2.5'] },
      message: boundaryAcceptanceError.integerValue('n', '2.5'),
    },
  ];

  /** The table's parameter, as the entry points receive it. */
  function rejectionParameter(parameter: Record<string, unknown>): Record<string, unknown> {
    return { name: 'n', values: [], ...parameter };
  }

  // The table only gates anything while its entries reach the model layer: an
  // entry the shape check turns away first makes every comparison below
  // trivially true, against a decision neither surface has made.
  it('is written against inputs the shared shape check lets through', () => {
    for (const { label, parameter } of modelLayerRejections) {
      expect(() => validateParameterShape([rejectionParameter(parameter)]), label).not.toThrow();
    }
  });

  for (const { label, parameter, message } of modelLayerRejections) {
    it(`rejects ${label} with one shared message`, () => {
      const input = {
        parameters: [rejectionParameter(parameter), { name: 'm', values: ['a', 'b'] }],
      } as unknown as GenerateInput;

      for (const { name, api } of surfaces) {
        const error = capture(() => api.generate(input));
        expect(error, name).toBeInstanceOf(Error);
        expect((error as CoverwiseError).code, name).toBe('INVALID_INPUT');
        expect((error as Error).message, name).toBe(message);
      }
    });
  }
});

// The documented input budgets are limits on the input, not on which function
// received it. Analysis takes the largest inputs the package accepts, so a
// surface that gates only its generator is the one where an oversized model
// reaches the engine.
describe('analyze judges the model by the same rules as generate', () => {
  it('rejects a model whose strings exceed the aggregate budget, identically', () => {
    const long = 'x'.repeat(MAX_STRING_BYTES - 16);
    const constraintCount = Math.ceil(MAX_AGGREGATE_STRING_BYTES / long.length) + 1;
    const parameters: Parameter[] = [{ name: 'n', values: [long, 'y'] }];
    const constraints = Array.from({ length: constraintCount }, () => `n = "${long}"`);

    const errors = surfaces.map(
      ({ api }) =>
        capture(() => api.analyzeCoverage(parameters, [{ n: 'y' }], 1, constraints)) as Error,
    );
    for (const [index, error] of errors.entries()) {
      expect(error, surfaces[index].name).toBeInstanceOf(Error);
      expect((error as CoverwiseError).code, surfaces[index].name).toBe('INVALID_INPUT');
      expect(error.message, surfaces[index].name).toBe(aggregateBudgetExceeded());
    }
    expect(errors[0].message).toBe(errors[1].message);
  });

  // A suite is the largest thing a caller submits, and the published budget
  // lists row count and aggregate string size in one breath. Charging only the
  // model left the dimension that actually grows without a documented
  // backstop, so an oversized suite reached the engine instead of the limit.
  it('rejects a suite whose rows exceed the aggregate budget, identically', () => {
    const cell = 'x'.repeat(60 * 1024);
    const rows = Math.ceil(MAX_AGGREGATE_STRING_BYTES / (2 * cell.length)) + 1;
    const parameters: Parameter[] = [
      { name: 'a', values: ['x', 'y'] },
      { name: 'b', values: ['1', '2'] },
    ];
    const tests: TestCase[] = Array.from({ length: rows }, () => ({ a: cell, b: cell }));

    const errors = surfaces.map(
      ({ api }) => capture(() => api.analyzeCoverage(parameters, tests, 2)) as Error,
    );
    for (const [index, error] of errors.entries()) {
      expect(error, surfaces[index].name).toBeInstanceOf(Error);
      expect((error as CoverwiseError).code, surfaces[index].name).toBe('INVALID_INPUT');
      expect(error.message, surfaces[index].name).toBe(aggregateBudgetExceeded());
    }
    expect(errors[0].message).toBe(errors[1].message);
  });

  it('rejects rows handed to extend on the same budget, identically', () => {
    const cell = 'x'.repeat(60 * 1024);
    const rows = Math.ceil(MAX_AGGREGATE_STRING_BYTES / (2 * cell.length)) + 1;
    const existing: TestCase[] = Array.from({ length: rows }, () => ({ a: cell, b: cell }));
    const input = {
      parameters: [
        { name: 'a', values: ['x', 'y'] },
        { name: 'b', values: ['1', '2'] },
      ],
    } as unknown as GenerateInput;

    const errors = surfaces.map(
      ({ api }) => capture(() => api.extendTests(existing, input)) as Error,
    );
    for (const [index, error] of errors.entries()) {
      expect(error, surfaces[index].name).toBeInstanceOf(Error);
      expect((error as CoverwiseError).code, surfaces[index].name).toBe('INVALID_INPUT');
      expect(error.message, surfaces[index].name).toBe(aggregateBudgetExceeded());
    }
    expect(errors[0].message).toBe(errors[1].message);
  });

  // The budget is one number for the whole call: a model and a suite that each
  // fit on their own must not pass together when their sum does not. The
  // expected sentence is quoted from the model layer rather than written here,
  // and budget.test.ts holds that sentence equal to the C++ one — so what these
  // two surfaces say about this input is the same text the CLI and the
  // embedding API say about it, without any of the four restating it.
  it('charges the model and the suite against one budget', () => {
    // Each cell is inside the per-string limit, and each side is inside the
    // aggregate one; only their sum is not.
    const cell = (index: number): string => `${index}`.padEnd(60 * 1024, 'x');
    const cells = Array.from({ length: 9 }, (_unused, index) => cell(index));
    const parameters = [
      { name: 'a', values: cells },
      { name: 'b', values: ['1', '2'] },
    ] as unknown as Parameter[];
    const tests: TestCase[] = cells.map((value) => ({ a: value, b: '1' }));

    for (const { name, api } of surfaces) {
      const error = capture(() => api.analyzeCoverage(parameters, tests, 2)) as Error;
      expect(error, name).toBeInstanceOf(Error);
      expect((error as CoverwiseError).code, name).toBe('INVALID_INPUT');
      expect(error.message, name).toBe(aggregateBudgetExceeded());
    }
  });

  // Weight keys are the one place caller text arrives as a key rather than a
  // value. A reader that walks values alone never sees them, so a model can
  // carry a megabyte of them and be charged nothing for it — while the engine
  // holds every byte.
  it('charges weight keys against the same budget as everything else', () => {
    // The model and the rows together sit just inside the budget, so the one
    // weight key decides. It is the caller's own text and the engine holds it,
    // but it arrives as a key: a reader that walks values alone never sees it
    // and lets this through.
    const text = (index: number): string => `${index}`.padEnd(60 * 1024, 'w');
    const values = Array.from({ length: 8 }, (_unused, index) => text(index));
    const parameters = [
      { name: 'a', values },
      { name: 'b', values: ['1', '2'] },
    ] as unknown as Parameter[];
    const existing: TestCase[] = Array.from({ length: 9 }, (_unused, index) => ({
      a: values[index % values.length],
      b: '1',
    }));
    const input = {
      parameters,
      weights: { a: { [text(99)]: 1 } },
    } as unknown as GenerateInput;

    for (const { name: surface, api } of surfaces) {
      const error = capture(() => api.extendTests(existing, input)) as CoverwiseError;
      expect(error, surface).toBeInstanceOf(Error);
      expect(error.code, surface).toBe('INVALID_INPUT');
      expect(error.message, surface).toBe(aggregateBudgetExceeded());
    }
  });

  // An empty model is refused for being empty, not for a strength the caller
  // never gave: naming the default in the diagnostic sends them to look at an
  // argument they did not write.
  it('rejects a model with no parameters, identically and for the right reason', () => {
    for (const strength of [undefined, 1, 5]) {
      const errors = surfaces.map(
        ({ api }) => capture(() => api.analyzeCoverage([], [], strength)) as CoverwiseError,
      );
      for (const [index, error] of errors.entries()) {
        const surface = `${surfaces[index].name} at strength ${String(strength)}`;
        expect(error, surface).toBeInstanceOf(Error);
        expect(error.code, surface).toBe('INVALID_INPUT');
        expect(error.message, surface).toBe('At least one parameter is required');
        expect(error.detail, surface).toBeUndefined();
      }
      expect(errors[0].message).toBe(errors[1].message);
      expect(errors[0].detail).toBe(errors[1].detail);
    }
  });

  it('rejects a boundary parameter the model layer refuses, identically', () => {
    const parameters = [
      { name: 'n', values: [], type: 'integer', range: [5, 1] },
    ] as unknown as Parameter[];

    for (const { name, api } of surfaces) {
      const error = capture(() => api.analyzeCoverage(parameters, [], 1)) as Error;
      expect(error, name).toBeInstanceOf(Error);
      expect((error as CoverwiseError).code, name).toBe('INVALID_INPUT');
      expect(error.message, name).toBe(boundaryAcceptanceError.range('n'));
    }
  });
});

// The shape checks in js/validation.ts run first for both package entry points,
// so they hide whatever the binding would have done on its own. An embedder
// calling the compiled module directly gets no such cover, and the answer must
// be the same: reject. Degrading to "no expansion" would hand back a suite
// generated over a value space the caller never described.
describe('the compiled module called directly', () => {
  interface RawModule {
    generate(input: unknown): { error?: true; code?: number; message?: string; tests?: unknown[] };
    extendTests(
      existing: unknown,
      input: unknown,
    ): { error?: true; code?: number; message?: string; tests?: unknown[] };
  }
  let raw: RawModule;

  beforeAll(async () => {
    // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
    const createModule = await import('../coverwise.js');
    raw = (await createModule.default()) as RawModule;
  });

  const malformed: Array<{ label: string; parameter: Record<string, unknown> }> = [
    { label: 'a non-numeric range', parameter: { type: 'integer', range: ['0', '10'] } },
    { label: 'a range that is not a pair', parameter: { type: 'integer', range: [0] } },
    { label: 'a range without a type', parameter: { range: [0, 10] } },
    { label: 'an unknown type', parameter: { type: 'decimal', range: [0, 10] } },
    { label: 'a non-numeric step', parameter: { type: 'float', range: [0, 10], step: 'two' } },
  ];

  for (const { label, parameter } of malformed) {
    it(`refuses to expand nothing when given ${label}`, () => {
      const result = raw.generate({
        parameters: [
          { name: 'n', values: ['5'], ...parameter },
          { name: 'm', values: ['a', 'b'] },
        ],
      });
      expect(result.error).toBe(true);
      expect(result.code).toBe(3);
      expect(result.tests).toBeUndefined();
    });
  }

  // The rule that a preserved row comes back as it was supplied belongs to the
  // renderer, not to each wrapper: the package entry points no longer re-apply
  // it, so the binding has to hold it on its own.
  it('echoes an existing row supplied by alias', () => {
    const row = { browser: 'Chrome', os: 'win' };
    const result = raw.extendTests([row], { parameters: aliasedBrowser });
    expect(result.error).toBeUndefined();
    expect((result.tests as TestCase[])[0]).toEqual(row);
  });

  it('still expands a well-formed boundary parameter', () => {
    const result = raw.generate({
      parameters: [
        { name: 'n', values: [], type: 'integer', range: [0, 10] },
        { name: 'm', values: ['a', 'b'] },
      ],
    });
    expect(result.error).toBeUndefined();
    const values = new Set((result.tests as Array<{ n: string }>).map((test) => test.n));
    expect([...values].sort()).toEqual(['-1', '0', '1', '10', '11', '9']);
  });
});

// A seed is asserted to be a real test case for this model, so its key set must
// match the declared parameter names exactly.
describe('seed rows', () => {
  it('rejects a row carrying an undeclared member on every surface', () => {
    const input = {
      parameters: twoBinaryParameters,
      seeds: [{ a: 'x', b: '1', note: 'hi' }],
    } as unknown as GenerateInput;

    for (const { api } of surfaces) {
      const error = capture(() => api.generate(input)) as CoverwiseError;
      expect(error.code).toBe('INVALID_INPUT');
      expect(error.message).toContain("unknown parameter 'note'");
    }
  });

  it('rejects a row whose value is outside the parameter domain', () => {
    const input = {
      parameters: twoBinaryParameters,
      seeds: [{ a: 'nope', b: '1' }],
    } as unknown as GenerateInput;

    for (const { api } of surfaces) {
      const error = capture(() => api.generate(input)) as CoverwiseError;
      expect(error.code).toBe('INVALID_INPUT');
    }
  });
});

// The number that decides whether a suite is too big is the documented row
// count, not the byte length of the document carrying it. The CLI accepts this
// same suite; it used to stop around a third of it because of a byte cap that
// no published limit mentioned.
describe('a suite at the documented row limit', () => {
  it('is accepted, and measured identically on both surfaces', () => {
    const parameters: Parameter[] = [
      { name: 'a', values: ['x', 'y'] },
      { name: 'b', values: ['1', '2'] },
      { name: 'c', values: ['p', 'q'] },
    ];
    const aValues = ['x', 'y'];
    const bValues = ['1', '2'];
    const cValues = ['p', 'q'];
    const tests: TestCase[] = Array.from({ length: 100_000 }, (_unused, i) => ({
      a: aValues[i % 2],
      b: bValues[Math.floor(i / 2) % 2],
      c: cValues[Math.floor(i / 4) % 2],
    }));

    const ratios = surfaces.map(({ api }) => api.analyzeCoverage(parameters, tests, 2));
    expect(ratios[0].coverageRatio).toBe(1);
    expect(ratios[1].coverageRatio).toBe(1);
    expect(ratios[0].invalidTests).toEqual([]);
    expect(ratios[1].invalidTests).toEqual([]);
  });

  it('is rejected one row past the limit, on both surfaces', () => {
    const parameters: Parameter[] = [{ name: 'a', values: ['x', 'y'] }];
    const tests: TestCase[] = Array.from({ length: 100_001 }, (_unused, i) => ({
      a: i % 2 === 0 ? 'x' : 'y',
    }));

    for (const { api } of surfaces) {
      const error = capture(() => api.analyzeCoverage(parameters, tests, 1)) as CoverwiseError;
      expect(error.code).toBe('INVALID_INPUT');
      expect(error.message).toContain('maximum is 100000 rows');
    }
  });
});

// What the budget charges, kind by kind.
//
// Measuring a ceiling shows that one fixture costs the same on two surfaces. It
// does not show that the same *set* of strings went into it: a reader that
// started charging row keys again, or stopped charging aliases, would move a
// ceiling by an amount nobody would recognise as either. These build a model
// carrying one instance of every kind the contract names, padded until the call
// sits exactly at the budget, and then add one instance of a single kind — so a
// failure names the kind rather than reporting a number that moved.
describe('what one call charges against the budget', () => {
  /** Under the per-string limit, so bulk is never refused for its own size. */
  const CELL = 60 * 1024;

  interface Model {
    parameters: Array<Record<string, unknown>>;
    constraints: string[];
    subModels: Array<{ parameters: string[]; strength: number }>;
    weights: Record<string, Record<string, number>>;
    existing: TestCase[];
    seeds?: TestCase[];
  }

  /** Filler of an exact byte length, distinct per tag. */
  const filler = (tag: string, bytes: number): string => tag.padEnd(bytes, 'p');

  /**
   * A model carrying one instance of every kind of string the contract names,
   * with `slack` bytes of adjustable text on top.
   *
   * The bulk sits in rows repeating values the model already declares, so the
   * model's own strings stay far below the budget while the call as a whole
   * approaches it. That keeps every case below decided by the reader that
   * charges the call rather than by the gate charging the model a second time.
   */
  function model(slack: number): Model {
    const bulk = [filler('b0', CELL), filler('b1', CELL)];
    const tail = filler('t', Math.max(slack, 1));
    return {
      parameters: [
        { name: 'a', values: [{ value: 'v0', aliases: ['al'], class: 'cl' }, { value: 'v1' }] },
        { name: 'b', values: ['x', 'y'] },
        { name: 'pad', values: [...bulk, tail] },
      ],
      constraints: ['a = "v0" OR a = "v1"'],
      subModels: [{ parameters: ['a', 'b'], strength: 2 }],
      weights: { a: { v0: 1 } },
      existing: [
        ...Array.from({ length: 15 }, (_unused, index) => ({ pad: bulk[index % bulk.length] })),
        { pad: tail },
      ],
    };
  }

  type Surface = (typeof surfaces)[number]['api'];

  /** The refusal this model draws, or null when it is accepted. */
  function refusal(api: Surface, built: Model): string | null {
    const { existing, ...input } = built;
    try {
      api.extendTests(existing, input as unknown as GenerateInput);
      return null;
    } catch (error) {
      return (error as Error).message;
    }
  }

  function overBudget(api: Surface, built: Model): boolean {
    return refusal(api, built) === aggregateBudgetExceeded();
  }

  /**
   * The largest slack `build` is accepted with — searched rather than stated,
   * so the cases below start from the budget itself and not from an arithmetic
   * they would have to keep in step with the accounting they are checking.
   */
  function calibrate(api: Surface, build: (slack: number) => Model): number {
    let low = 1;
    let high = CELL;
    while (low < high) {
      const mid = Math.ceil((low + high) / 2);
      if (refusal(api, build(mid)) === null) {
        low = mid;
      } else {
        high = mid - 1;
      }
    }
    return low;
  }

  let slack = 0;

  beforeAll(() => {
    slack = calibrate(pure, model);
  });

  it('sits exactly at the budget on both surfaces', () => {
    for (const { name, api } of surfaces) {
      expect(refusal(api, model(slack)), name).toBeNull();
      expect(overBudget(api, model(slack + 1)), name).toBe(true);
    }
  });

  /** One more instance of a kind the budget charges. */
  const charged: Array<{ kind: string; add: (built: Model, text: string) => void }> = [
    {
      kind: 'a parameter name',
      add: (built, text) => built.parameters.push({ name: text, values: ['q'] }),
    },
    {
      kind: 'a value',
      add: (built, text) => (built.parameters[1].values as string[]).push(text),
    },
    {
      kind: 'an alias',
      add: (built, text) => {
        const values = built.parameters[0].values as Array<{ aliases?: string[] }>;
        values[0].aliases = ['al', text];
      },
    },
    {
      kind: 'a class name',
      add: (built, text) => {
        const values = built.parameters[0].values as Array<{ value: string; class?: string }>;
        values[1] = { value: 'v1', class: text };
      },
    },
    {
      kind: 'a constraint expression',
      add: (built, text) => built.constraints.push(`b = "${text}"`),
    },
    {
      kind: 'a sub-model parameter name',
      add: (built, text) => built.subModels.push({ parameters: [text], strength: 1 }),
    },
    {
      kind: 'a weight parameter name',
      add: (built, text) => {
        built.weights[text] = { q: 1 };
      },
    },
    {
      kind: 'a weight value name',
      add: (built, text) => {
        built.weights.a[text] = 1;
      },
    },
    {
      kind: 'a string row value',
      add: (built, text) => built.existing.push({ b: text }),
    },
  ];

  for (const { kind, add } of charged) {
    it(`charges ${kind}`, () => {
      for (const { name, api } of surfaces) {
        const built = model(slack);
        add(built, filler('extra', 64));
        expect(overBudget(api, built), `${name}: ${kind}`).toBe(true);
      }
    });
  }

  /** Text the caller supplies that the engine never holds, and so never pays for. */
  const uncharged: Array<{ kind: string; add: (built: Model) => void }> = [
    {
      // A row key is a parameter name the model already declares and paid for.
      kind: 'a row key',
      add: (built) => {
        for (let index = 0; index < 5; ++index) {
          built.existing.push({ [filler(`k${index}`, 2048)]: 1 });
        }
      },
    },
    {
      kind: 'a numeric row value',
      add: (built) => {
        for (let index = 0; index < 20; ++index) {
          built.existing.push({ a: 1234567890123456 });
        }
      },
    },
    {
      kind: 'a boolean row value',
      add: (built) => {
        for (let index = 0; index < 20; ++index) {
          built.existing.push({ a: true });
        }
      },
    },
  ];

  for (const { kind, add } of uncharged) {
    it(`does not charge ${kind}`, () => {
      for (const { name, api } of surfaces) {
        const built = model(slack);
        add(built);
        expect(refusal(api, built), `${name}: ${kind}`).toBeNull();
      }
    });
  }

  // A row given as a seed is read by two checks in the same call. Charged by
  // each of them it would cost twice what the caller wrote, and the budget
  // would be half the published one for anyone who uses seeds. Calibrating the
  // same model with and without the seed measures how many times its text was
  // charged, rather than asserting one arrangement of bytes happens to fit.
  it('charges a seed row once, where it is read', () => {
    const withSeed = (bytes: number): Model => {
      const built = model(bytes);
      const tail = (built.parameters[2].values as string[])[2];
      built.seeds = [{ a: 'v0', b: 'x', pad: tail }];
      return built;
    };

    for (const { name, api } of surfaces) {
      const seedSlack = calibrate(api, withSeed);
      // What the seed itself spells: the adjustable tail and the two short
      // values beside it.
      const seedBytes = seedSlack + 'v0'.length + 'x'.length;
      const charges = Math.round((2 * slack - 2 * seedSlack) / seedBytes);
      expect(charges, name).toBe(1);
    }
  });
});
