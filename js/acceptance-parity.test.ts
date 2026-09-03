/// Cross-surface acceptance: the same JSON is accepted, rejected, or degraded
/// the same way by the WASM-backed and pure-TypeScript entry points.
///
/// The expectations here are written to the same numbers the CLI integration
/// tests assert (tests/integration/cli_test.cpp), so a divergence between the
/// native and JavaScript surfaces shows up as a failure on one side or the
/// other rather than as two suites that quietly disagree.

import { beforeAll, describe, expect, it } from 'vitest';
import { generate as internalGenerate } from '../src/ts/core/generator.js';
import { boundaryAcceptanceError } from '../src/ts/model/boundary-rules.js';
import {
  aggregateBudgetExceeded,
  CHARGED_STRING_KINDS,
  type ChargedStringKind,
  chargedStringContext,
  stringBudgetExceeded,
} from '../src/ts/model/budget.js';
import { createGenerateOptions } from '../src/ts/model/generate-options.js';
import { MAX_AGGREGATE_STRING_BYTES, MAX_STRING_BYTES } from '../src/ts/model/limits.js';
import { UNASSIGNED } from '../src/ts/model/test-case.js';
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

// ---------------------------------------------------------------------------
// The documented byte budgets, across every surface at once
//
// One input, driven through every surface the package reaches, with both the
// verdict and the refusal compared byte for byte. Two things make that hold up
// over time. The surfaces are a table rather than a pair of imports, so a
// surface added later has to be answered for; and the kinds of string the
// budget charges are the shared list itself, so a kind added there without a
// case here does not compile. A surface or a kind that quietly stops being
// covered is how a seam survives a review.
//
// Measuring a ceiling alone would show only that one fixture costs the same
// twice. It would not show that the same *set* of strings went into it: a
// reader that started charging row keys again, or stopped charging aliases,
// moves a ceiling by an amount nobody would recognise as either. So the model
// below carries one instance of every kind the contract names, padded until the
// call sits exactly at the budget, and each case adds one instance of a single
// kind — a failure names the kind rather than reporting a number that moved.
// ---------------------------------------------------------------------------

/** The JavaScript-reachable surfaces that share the acceptance contract. */
const ACCEPTANCE_SURFACES = ['wasm', 'pure', 'module'] as const;
type AcceptanceSurface = (typeof ACCEPTANCE_SURFACES)[number];

/** The calls that judge an input, as every surface offers them. */
const ENTRY_POINTS = ['generate', 'extend', 'analyze'] as const;
type EntryPoint = (typeof ENTRY_POINTS)[number];

/** A model as the package entry points receive it, plus the rows beside it. */
interface Model {
  parameters: Array<Record<string, unknown>>;
  constraints: string[];
  subModels: Array<{ parameters: string[]; strength: number }>;
  weights: Record<string, Record<string, number>>;
  existing: TestCase[];
  seeds?: TestCase[];
  /** Fields the schema does not read, which some cases below add on purpose. */
  [field: string]: unknown;
}

/** What the compiled module returns instead of throwing. */
interface RawResult {
  error?: true;
  message?: string;
}

interface RawBudgetModule {
  generate(input: unknown): RawResult;
  extendTests(existing: unknown, input: unknown): RawResult;
  analyzeCoverage(
    parameters: unknown,
    tests: unknown,
    strength: number,
    constraints: unknown,
  ): RawResult;
}

describe('the documented byte budgets', () => {
  let raw: RawBudgetModule;

  /** Under the per-string limit, so bulk is never refused for its own size. */
  const CELL = 60 * 1024;

  /** Filler of an exact byte length, distinct per tag. */
  const filler = (tag: string, bytes: number): string => tag.padEnd(bytes, 'p');

  /** The model without the rows that travel beside it. */
  function inputOf(built: Model): GenerateInput {
    const { existing: _existing, ...input } = built;
    return input as unknown as GenerateInput;
  }

  /** The refusal a thrown-error surface gives, or null when it accepts. */
  function thrown(run: () => unknown): string | null {
    try {
      run();
      return null;
    } catch (error) {
      return (error as Error).message;
    }
  }

  /** The refusal the compiled module gives, or null when it accepts. */
  function reported(run: () => RawResult): string | null {
    try {
      const result = run();
      return result.error === true ? (result.message ?? '') : null;
    } catch (error) {
      return (error as Error).message;
    }
  }

  /**
   * Every surface's every entry point, as one uniform question: what does this
   * call say about this model?
   *
   * Written as a map keyed by the surface and entry-point unions rather than as
   * a list, so neither a new surface nor a new entry point can be added without
   * an answer here.
   */
  const drive: Record<AcceptanceSurface, Record<EntryPoint, (built: Model) => string | null>> = {
    wasm: {
      generate: (built) => thrown(() => wasm.generate(inputOf(built))),
      extend: (built) => thrown(() => wasm.extendTests(built.existing, inputOf(built))),
      analyze: (built) =>
        thrown(() =>
          wasm.analyzeCoverage(
            built.parameters as unknown as Parameter[],
            built.existing,
            2,
            built.constraints,
          ),
        ),
    },
    pure: {
      generate: (built) => thrown(() => pure.generate(inputOf(built))),
      extend: (built) => thrown(() => pure.extendTests(built.existing, inputOf(built))),
      analyze: (built) =>
        thrown(() =>
          pure.analyzeCoverage(
            built.parameters as unknown as Parameter[],
            built.existing,
            2,
            built.constraints,
          ),
        ),
    },
    module: {
      generate: (built) => reported(() => raw.generate(inputOf(built))),
      extend: (built) => reported(() => raw.extendTests(built.existing, inputOf(built))),
      analyze: (built) =>
        reported(() => raw.analyzeCoverage(built.parameters, built.existing, 2, built.constraints)),
    },
  };

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

  /** What every surface says about `built` when asked through extend. */
  function refusals(built: Model): Record<AcceptanceSurface, string | null> {
    return Object.fromEntries(
      ACCEPTANCE_SURFACES.map((surface) => [surface, drive[surface].extend(built)]),
    ) as Record<AcceptanceSurface, string | null>;
  }

  /**
   * Assert every surface answered `expected`, byte for byte.
   *
   * Comparing the surfaces to each other would pass while all of them are
   * wrong, and comparing each to a sentence spelled out here would be another
   * copy of the wording. The expected text is quoted from the shared generator,
   * which budget.test.ts holds equal to the C++ one — so what these surfaces say
   * is the same text the command line says, without any of them restating it.
   */
  function expectAll(built: Model, expected: string | null, label: string): void {
    const answers = refusals(built);
    for (const surface of ACCEPTANCE_SURFACES) {
      expect(answers[surface], `${label} on ${surface}`).toBe(expected);
    }
  }

  /**
   * The largest slack `build` is accepted with — searched rather than stated,
   * so the cases below start from the budget itself and not from an arithmetic
   * they would have to keep in step with the accounting they are checking.
   */
  function calibrate(
    drives: (built: Model) => string | null,
    build: (slack: number) => Model,
  ): number {
    let low = 1;
    let high = CELL;
    while (low < high) {
      const mid = Math.ceil((low + high) / 2);
      if (drives(build(mid)) === null) {
        low = mid;
      } else {
        high = mid - 1;
      }
    }
    return low;
  }

  let slack = 0;

  beforeAll(async () => {
    // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
    const createModule = await import('../coverwise.js');
    raw = (await createModule.default()) as RawBudgetModule;
    // Searched on one surface and then required of all of them: a ceiling the
    // surfaces disagree about fails the case below rather than moving with
    // whichever surface was asked to find it.
    slack = calibrate(drive.pure.extend, model);
  });

  it('puts the ceiling in the same place on every surface', () => {
    expectAll(model(slack), null, 'at the ceiling');
    expectAll(model(slack + 1), aggregateBudgetExceeded(), 'one byte past the ceiling');
  });

  // The budget is one number for the whole call, and one number for the whole
  // package: whichever call a caller reached for, an input over the limit is
  // refused, in the same words. A surface that gated only its generator is
  // where an oversized model reaches the engine through analysis instead.
  describe('every entry point refuses the same input', () => {
    /**
     * A model whose own strings are over the budget.
     *
     * The bulk sits in the parameters rather than in the rows, because that is
     * the only part of an input all three calls read: generate is handed no
     * rows at all, and analysis is handed no weights or sub-models.
     */
    function overBudget(): Model {
      const wide = Math.ceil(MAX_AGGREGATE_STRING_BYTES / CELL) + 1;
      return {
        parameters: [
          {
            name: 'a',
            values: Array.from({ length: wide }, (_unused, index) => filler(`w${index}`, CELL)),
          },
          { name: 'b', values: ['x', 'y'] },
        ],
        constraints: [],
        subModels: [],
        weights: {},
        existing: [{ a: filler('w0', CELL), b: 'x' }],
      };
    }

    for (const surface of ACCEPTANCE_SURFACES) {
      for (const entry of ENTRY_POINTS) {
        it(`refuses it at ${surface}.${entry}`, () => {
          expect(drive[surface][entry](overBudget())).toBe(aggregateBudgetExceeded());
        });
      }
    }
  });

  /**
   * One more instance of a kind the budget charges.
   *
   * Keyed by the shared list of kinds, so a kind added to the contract without
   * a case here is a type error rather than an omission nobody notices.
   */
  const charged: Record<ChargedStringKind, (built: Model, text: string) => void> = {
    parameterName: (built, text) => built.parameters.push({ name: text, values: ['q'] }),
    parameterValue: (built, text) => (built.parameters[1].values as string[]).push(text),
    valueAlias: (built, text) => {
      const values = built.parameters[0].values as Array<{ aliases?: string[] }>;
      values[0].aliases = ['al', text];
    },
    equivalenceClass: (built, text) => {
      const values = built.parameters[0].values as Array<{ value: string; class?: string }>;
      values[1] = { value: 'v1', class: text };
    },
    constraintExpression: (built, text) => built.constraints.push(`b = "${text}"`),
    subModelParameterName: (built, text) =>
      built.subModels.push({ parameters: [text], strength: 1 }),
    weightParameterName: (built, text) => {
      built.weights[text] = { q: 1 };
    },
    weightValueName: (built, text) => {
      built.weights.a[text] = 1;
    },
    rowValue: (built, text) => built.existing.push({ b: text }),
  };

  for (const kind of CHARGED_STRING_KINDS) {
    it(`charges ${kind}`, () => {
      const built = model(slack);
      charged[kind](built, filler('extra', 64));
      expectAll(built, aggregateBudgetExceeded(), `one more ${kind}`);
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
    {
      // The schema never reads it, so the engine never holds it. Charging it
      // would refuse a model carrying its own notes on one surface while the
      // command line — which walks the fields it parses — generates for it.
      kind: 'an unknown top-level field',
      add: (built) => {
        built.description = filler('d', CELL);
        built.$schema = filler('s', CELL);
      },
    },
    {
      kind: 'an unknown nested field',
      add: (built) => {
        built.parameters[0].description = filler('n', CELL);
        (built.parameters[0].values as Array<Record<string, unknown>>)[0].note = filler('v', CELL);
      },
    },
  ];

  for (const { kind, add } of uncharged) {
    it(`does not charge ${kind}`, () => {
      const built = model(slack);
      add(built);
      expectAll(built, null, `an added ${kind}`);
    });
  }

  // A row's keys are not charged, but the text under a key is — whether or not
  // that key names a declared parameter. What the limit bounds is what the
  // caller handed over, not the part of it the model happened to have somewhere
  // to put; a surface that dropped an unrecognised key before counting accepts
  // a suite every other surface refuses.
  it('charges a row value under a key that names no parameter', () => {
    const built = model(slack);
    built.existing.push({ undeclared: filler('u', 64) });
    expectAll(built, aggregateBudgetExceeded(), 'a value under an undeclared key');
  });

  // The engine can also be reached directly, with options a caller built rather
  // than a document a surface read. Nothing is in front of that entry to count
  // the caller's row text, so the gate charges it there — and it has to refuse
  // in the words the reader-backed surfaces use for the same suite, or the two
  // accounting regimes are two contracts wearing one sentence.
  it('refuses row text at the direct engine entry in the same words', () => {
    const text = filler('r', CELL);
    const rows = Math.ceil(MAX_AGGREGATE_STRING_BYTES / (2 * text.length)) + 1;
    const parameters = [
      { name: 'a', values: ['x', 'y'] },
      { name: 'b', values: ['1', '2'] },
    ];
    const built: Model = {
      parameters,
      constraints: [],
      subModels: [],
      weights: {},
      existing: Array.from({ length: rows }, () => ({ a: text, b: text })),
    };
    expectAll(built, aggregateBudgetExceeded(), 'row text over the ceiling');

    // The same suite as the engine receives it: value indices that did not
    // resolve, with the caller's own text kept beside them.
    const direct = internalGenerate(
      createGenerateOptions({
        parameters: parameters.map((parameter) => ({
          name: parameter.name,
          values: parameter.values,
        })),
        seeds: Array.from({ length: rows }, () => ({
          values: [UNASSIGNED, UNASSIGNED],
          unresolved: [text, text],
        })),
      }),
    );
    expect(direct.error.message).toBe(aggregateBudgetExceeded());
  });

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

    for (const surface of ACCEPTANCE_SURFACES) {
      const seedSlack = calibrate(drive[surface].extend, withSeed);
      // What the seed itself spells: the adjustable tail and the two short
      // values beside it.
      const seedBytes = seedSlack + 'v0'.length + 'x'.length;
      const charges = Math.round((2 * slack - 2 * seedSlack) / seedBytes);
      expect(charges, surface).toBe(1);
    }
  });

  // The per-string limit has one sentence too, and it names the string it
  // refused. Every kind is driven so a wording changed for one of them cannot
  // stay changed for that one alone.
  describe('the per-string limit names the string it refused', () => {
    const oversized = 'x'.repeat(MAX_STRING_BYTES + 1);

    /** A small model, so only the one oversized string decides. */
    function small(): Model {
      return {
        parameters: [
          { name: 'a', values: [{ value: 'v0', aliases: ['al'], class: 'cl' }, { value: 'v1' }] },
          { name: 'b', values: ['x', 'y'] },
        ],
        constraints: [],
        subModels: [],
        weights: {},
        existing: [{ a: 'v0', b: 'x' }],
      };
    }

    const contexts: Record<ChargedStringKind, string> = {
      parameterName: chargedStringContext.parameterName(oversized),
      parameterValue: chargedStringContext.parameterValue('b', 2),
      valueAlias: chargedStringContext.valueAlias('a', 0),
      equivalenceClass: chargedStringContext.equivalenceClass('a', 1),
      constraintExpression: chargedStringContext.constraintExpression(),
      subModelParameterName: chargedStringContext.subModelParameterName(),
      weightParameterName: chargedStringContext.weightParameterName(),
      weightValueName: chargedStringContext.weightValueName(),
      rowValue: chargedStringContext.rowValue('existing', 1),
    };

    /** Where the oversized string of each kind is written into the model. */
    const place: Record<ChargedStringKind, (built: Model) => void> = {
      parameterName: (built) => built.parameters.push({ name: oversized, values: ['q'] }),
      parameterValue: (built) => (built.parameters[1].values as string[]).push(oversized),
      valueAlias: (built) => {
        const values = built.parameters[0].values as Array<{ aliases?: string[] }>;
        values[0].aliases = [oversized];
      },
      equivalenceClass: (built) => {
        const values = built.parameters[0].values as Array<{ value: string; class?: string }>;
        values[1] = { value: 'v1', class: oversized };
      },
      constraintExpression: (built) => built.constraints.push(oversized),
      subModelParameterName: (built) =>
        built.subModels.push({ parameters: [oversized], strength: 1 }),
      weightParameterName: (built) => {
        built.weights[oversized] = { v0: 1 };
      },
      weightValueName: (built) => {
        built.weights.a = { [oversized]: 1 };
      },
      rowValue: (built) => built.existing.push({ b: oversized }),
    };

    for (const kind of CHARGED_STRING_KINDS) {
      it(`refuses an oversized ${kind} in one wording`, () => {
        const built = small();
        place[kind](built);
        expectAll(built, stringBudgetExceeded(contexts[kind]), `an oversized ${kind}`);
      });
    }
  });
});

// A model can have more than one thing wrong with it, and the gate reports the
// first one it reaches. That makes the order it walks the caller's maps in part
// of the contract: the core keeps them sorted and a JavaScript object keeps them
// in insertion order, so a surface walking the object as written would name a
// different parameter for the same input and give two callers two accounts of
// one model.
describe('which violator a surface names when a model has several', () => {
  const reversedRange = {
    values: [],
    type: 'integer' as const,
    range: [10, 0] as [number, number],
  };

  it('names the same boundary parameter on every surface', () => {
    const input = {
      parameters: [
        { name: 'zzz', ...reversedRange },
        { name: 'aaa', ...reversedRange },
      ],
    } as GenerateInput;

    for (const { name, api } of surfaces) {
      const thrown = capture(() => api.generate(input));
      expect(thrown, name).toBeInstanceOf(Error);
      expect((thrown as Error).message, name).toBe(boundaryAcceptanceError.range('aaa'));
    }
  });
});
