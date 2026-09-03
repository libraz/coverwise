import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from '../../js/index.js';
import * as pure from '../../js/pure/index.js';
import type { GenerateInput, Parameter, TestCase } from '../../js/types.js';

/// A suite generated before a model's values were re-cased, or written by hand
/// against a runner that spells them differently, is the ordinary thing to feed
/// back in. Every value name a caller writes -- a seed, a `tests` row, an
/// `existing` row, a weights key -- resolves by ASCII case folding, and to the
/// same value the declared spelling names.
///
/// Three surfaces are driven here: the compiled module called directly, the
/// `@libraz/coverwise` wrapper over it, and the pure-TypeScript port. The
/// native CLI is the fourth, and it asserts the same property in
/// tests/integration/cli_test.cpp.

interface RawResult {
  error?: true;
  code?: number;
  message?: string;
  tests?: Array<Record<string, string>>;
  invalidTests?: Array<{ testIndex: number; reason: string }>;
  coveredTuples?: number;
  stats?: { testCount: number };
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
}

let raw: RawModule;

/// Every declared spelling is mixed case, so a name written in another case is
/// something the surface has to fold rather than something it matches by luck.
const parameters: Parameter[] = [
  { name: 'os', values: ['Windows', 'Linux'] },
  { name: 'browser', values: ['Chrome', 'Firefox'] },
];

/// The same two rows, spelled as the model declares them and in another case.
const declaredRows: TestCase[] = [
  { os: 'Windows', browser: 'Chrome' },
  { os: 'Linux', browser: 'Firefox' },
];
const otherCaseRows: TestCase[] = [
  { os: 'wINDOWS', browser: 'chrome' },
  { os: 'LINUX', browser: 'FireFOX' },
];

/// A model whose seeds, constraint and weights are spelled as declared, and the
/// same model with every one of those names in a different case.
function declaredModel(): GenerateInput {
  return {
    parameters,
    strength: 2,
    seed: 3,
    seeds: [{ os: 'Windows', browser: 'Chrome' }],
    constraints: ['IF os = Linux THEN browser != Firefox'],
    weights: { browser: { Firefox: 9 } },
  };
}

function otherCaseModel(): GenerateInput {
  return {
    parameters,
    strength: 2,
    seed: 3,
    seeds: [{ os: 'wInDoWs', browser: 'cHROME' }],
    constraints: ['IF os = LINUX THEN browser != firefox'],
    weights: { browser: { fIREFOx: 9 } },
  };
}

/// A model whose values differ only in the case of a non-ASCII letter, which the
/// ASCII fold does not reach.
const nonAsciiParameters: Parameter[] = [
  { name: 'city', values: ['MÜNCHEN', 'OSAKA'] },
  { name: 'n', values: ['1', '2'] },
];

/// A parameter with aliases, so a weights key can name one value by more than
/// one spelling.
const aliasedParameters: Parameter[] = [
  { name: 'p', values: [{ value: 'Chromium', aliases: ['Chrome', 'Edge'] }, 'Firefox'] },
  { name: 'q', values: ['0', '1'] },
];

/// Every way two weights keys can name one value, and the verdict each earns.
/// A model is only weighted the same everywhere if the surfaces agree on all of
/// them, message included.
const weightKeyPairs: Array<{
  label: string;
  parameters: Parameter[];
  weights: Record<string, Record<string, number>>;
  message?: string;
}> = [
  {
    label: 'two spellings neither of which is declared',
    parameters,
    weights: { os: { wINdows: 5, WINDOWS: 9 } },
    message: 'Ambiguous value in weights: os=WINDOWS and os=wINdows name the same value',
  },
  {
    label: 'the declared spelling beside a folded one',
    parameters,
    weights: { os: { Windows: 5, wINdows: 9 } },
  },
  {
    label: 'the declared spelling beside one of its aliases',
    parameters: aliasedParameters,
    weights: { p: { Chromium: 5, Chrome: 9 } },
  },
  {
    label: 'two aliases of one value',
    parameters: aliasedParameters,
    weights: { p: { Chrome: 5, Edge: 9 } },
    message: 'Ambiguous value in weights: p=Chrome and p=Edge name the same value',
  },
  {
    label: 'an alias beside a folded spelling of that alias',
    parameters: aliasedParameters,
    weights: { p: { Chrome: 5, cHROME: 9 } },
    message: 'Ambiguous value in weights: p=Chrome and p=cHROME name the same value',
  },
  {
    label: 'two keys naming two different values',
    parameters,
    weights: { os: { wINdows: 5, LINUX: 9 } },
  },
];

const packages = [
  { name: 'npm', api: wasm },
  { name: 'pure', api: pure },
] as const;

beforeAll(async () => {
  await wasm.init();
  // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
  const createModule = await import('../coverwise.js');
  raw = (await createModule.default()) as RawModule;
});

describe('the compiled module called directly', () => {
  it('generates the same suite whatever case the seeds, constraint and weights use', () => {
    const declared = raw.generate(declaredModel());
    const other = raw.generate(otherCaseModel());

    expect(declared.error).toBeUndefined();
    expect(other.error).toBeUndefined();
    expect(other.tests).toEqual(declared.tests);
  });

  it('credits an analyzed row written in another case', () => {
    const declared = raw.analyzeCoverage(parameters, declaredRows, 2, []);
    const other = raw.analyzeCoverage(parameters, otherCaseRows, 2, []);

    expect(other.invalidTests).toEqual([]);
    expect(other.coveredTuples).toBe(declared.coveredTuples);
    expect(other.coveredTuples).toBe(2);
  });

  it('keeps the coverage credit of an existing row written in another case', () => {
    const model = { parameters, strength: 2, seed: 3 };
    const declared = raw.extendTests(declaredRows, model);
    const other = raw.extendTests(otherCaseRows, model);

    // Rows are handed back in the spelling they were supplied in, so the count
    // is what says the credit was kept: a suite that lost it would need more
    // generated rows to reach the same coverage.
    expect(other.stats?.testCount).toBe(declared.stats?.testCount);
    expect(other.stats?.testCount).toBe(4);
  });

  it('leaves a name differing only in a non-ASCII case unresolved', () => {
    const result = raw.analyzeCoverage(nonAsciiParameters, [{ city: 'MüNCHEN', n: '1' }], 2, []);

    expect(result.invalidTests).toEqual([
      { testIndex: 0, reason: "value 'MüNCHEN' is not declared by parameter city" },
    ]);
  });
});

for (const { name, api } of packages) {
  describe(`the ${name} package`, () => {
    it('generates the same suite whatever case the seeds, constraint and weights use', () => {
      const declared = api.generate(declaredModel());
      const other = api.generate(otherCaseModel());

      expect(other.tests).toEqual(declared.tests);
      expect(other.warnings).toEqual([]);
    });

    it('credits an analyzed row written in another case', () => {
      const declared = api.analyzeCoverage(parameters, declaredRows, 2);
      const other = api.analyzeCoverage(parameters, otherCaseRows, 2);

      expect(other.invalidTests).toEqual([]);
      expect(other.coveredTuples).toBe(declared.coveredTuples);
      expect(other.coveredTuples).toBe(2);
    });

    it('keeps the coverage credit of an existing row written in another case', () => {
      const model = { parameters, strength: 2, seed: 3 };
      const declared = api.extendTests(declaredRows, model);
      const other = api.extendTests(otherCaseRows, model);

      expect(other.tests.length).toBe(declared.tests.length);
      expect(other.tests.length).toBe(4);
      expect(other.warnings).toEqual([]);
    });

    it('refuses a seed differing only in a non-ASCII case rather than folding it', () => {
      expect(() =>
        api.generate({
          parameters: nonAsciiParameters,
          seeds: [{ city: 'MüNCHEN', n: '1' }],
        }),
      ).toThrow(/MüNCHEN/);

      // The ASCII half of the same value still folds, so this is the fold's
      // reach and not an absence of folding.
      const folded = api.analyzeCoverage(nonAsciiParameters, [{ city: 'osaka', n: '1' }], 2);
      expect(folded.invalidTests).toEqual([]);
      expect(folded.coveredTuples).toBe(1);
    });

    it('weights the value a key in another case names', () => {
      const base = { parameters, strength: 2, seed: 7 };
      const unweighted = api.generate(base);
      const declared = api.generate({ ...base, weights: { browser: { Firefox: 50 } } });
      const other = api.generate({ ...base, weights: { browser: { fIREFOx: 50 } } });

      // The weight has to change the suite for this case to measure anything.
      expect(declared.tests).not.toEqual(unweighted.tests);
      expect(other.tests).toEqual(declared.tests);
    });

    // Two keys naming one value carry two weights for it, and only one can
    // apply. A key spelled the way the model declares the value settles that;
    // with no declared spelling among them the winner would come down to the
    // order the map is walked in, which is sorted in the core and insertion
    // order in a JavaScript object, so the model is refused instead.
    for (const { label, parameters: params, weights, message } of weightKeyPairs) {
      it(`${message ? 'refuses' : 'accepts'} ${label}`, () => {
        const input = { parameters: params, strength: 2, seed: 3, weights };
        if (message === undefined) {
          expect(() => api.generate(input)).not.toThrow();
          return;
        }
        expect(() => api.generate(input)).toThrow(message);
      });
    }

    // The core keeps a weights map sorted and a JavaScript object keeps it in
    // insertion order, so a map with two things wrong with it has to be refused
    // over the same key on both, or the same input reports different problems
    // depending on where it ran.
    it('names the same key when more than one is unknown', () => {
      expect(() => api.generate({ parameters, weights: { os: { zzz: 1, aaa: 1 } } })).toThrow(
        'Unknown value in weights: os=aaa',
      );
    });
  });
}
