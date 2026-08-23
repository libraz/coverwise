/// Cross-surface acceptance: the same JSON is accepted, rejected, or degraded
/// the same way by the WASM-backed and pure-TypeScript entry points.
///
/// The expectations here are written to the same numbers the CLI integration
/// tests assert (tests/integration/cli_test.cpp), so a divergence between the
/// native and JavaScript surfaces shows up as a failure on one side or the
/// other rather than as two suites that quietly disagree.

import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from './index.js';
import * as pure from './pure/index.js';
import type { CoverwiseError, GenerateInput, Parameter, TestCase } from './types.js';

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

  // Integer expansion steps by one, so any other step describes a value set the
  // engine will not produce.
  it('rejects an integer step other than 1 with one shared message', () => {
    const input = {
      parameters: [
        { name: 'n', type: 'integer', range: [0, 10], step: 5, values: [] },
        { name: 'm', values: ['a', 'b'] },
      ],
    } as unknown as GenerateInput;

    const errors = surfaces.map(({ api }) => capture(() => api.generate(input)) as CoverwiseError);
    for (const error of errors) {
      expect(error.code).toBe('INVALID_INPUT');
      expect(error.message).toBe('Integer boundary step must be 1 for parameter n');
    }
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

  // A shape the reader cannot convert is an error on every surface. The path
  // that used to matter here is the one that skipped expansion instead and
  // generated over the declared values.
  const malformedShapes: Array<{ label: string; parameter: Record<string, unknown> }> = [
    { label: 'a non-numeric range', parameter: { type: 'integer', range: ['0', '10'] } },
    { label: 'a range that is not a pair', parameter: { type: 'integer', range: [0] } },
    { label: 'a range without a type', parameter: { range: [0, 10] } },
    { label: 'an unknown type', parameter: { type: 'decimal', range: [0, 10] } },
    { label: 'a non-numeric step', parameter: { type: 'float', range: [0, 10], step: 'two' } },
  ];

  for (const { label, parameter } of malformedShapes) {
    it(`rejects ${label} on every surface`, () => {
      const input = {
        parameters: [
          { name: 'n', values: [], ...parameter },
          { name: 'm', values: ['a', 'b'] },
        ],
      } as unknown as GenerateInput;

      const errors = surfaces.map(({ api }) => capture(() => api.generate(input)));
      for (const error of errors) {
        expect(error).toBeInstanceOf(Error);
        expect((error as CoverwiseError).code).toBe('INVALID_INPUT');
      }
      expect((errors[0] as Error).message).toBe((errors[1] as Error).message);
    });
  }
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
