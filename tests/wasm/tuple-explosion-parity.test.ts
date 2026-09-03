import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from '../../js/index.js';
import { CoverwiseError } from '../../js/index.js';
import * as pure from '../../js/pure/index.js';
import type { GenerateInput, Parameter } from '../../js/types.js';

/// A model too wide to count is described by its diagnostic, and the figure in
/// that diagnostic is the only thing a caller has to size the model by. The
/// count is a uint64 in the core, so past 2^53 a double no longer holds it and
/// past 2^64 the core stops counting and reports the ceiling it saturates at.
/// Both points are reached by models these surfaces accept, so the
/// pure-TypeScript port has to arrive at the same figure at both of them rather
/// than at a rounded or an infinite one.
///
/// The expected text is read off the compiled module rather than written here:
/// that is the core speaking, which is what makes this a parity assertion
/// instead of two copies of one string.

interface RawResult {
  error?: true;
  code?: number;
  message?: string;
  detail?: string;
}

interface RawModule {
  generate(input: unknown): RawResult;
}

let raw: RawModule;

/// A model of `count` parameters holding `size` values each, covered at a
/// strength that takes all of them, so a single combination decides the count.
function uniformModel(count: number, size: number): GenerateInput {
  const values = Array.from({ length: size }, (_, j) => `v${j}`);
  const parameters: Parameter[] = Array.from({ length: count }, (_, i) => ({
    name: `p${i}`,
    values,
  }));
  return { parameters, strength: count, seed: 1 };
}

/// The two points the count passes on its way out of range.
const models = [
  // 3^34 is odd and larger than 2^53, so a double has to round it.
  { name: 'a count above 2^53', model: () => uniformModel(34, 3) },
  // 64^11 = 2^66, past what the core's uint64 accumulator holds.
  { name: 'a count above 2^64', model: () => uniformModel(11, 64) },
] as const;

const packages = [
  { name: 'npm', api: wasm },
  { name: 'pure', api: pure },
] as const;

/// The refusal a surface answers a model past the tuple limit with. Anything
/// else -- a suite, some other error -- is a failure of the test's premise
/// rather than of the figure it is about to compare.
function refusalOf(api: typeof wasm | typeof pure, model: GenerateInput): CoverwiseError {
  let thrown: unknown;
  try {
    api.generate(model);
  } catch (error) {
    thrown = error;
  }
  expect(thrown).toBeInstanceOf(CoverwiseError);
  return thrown as CoverwiseError;
}

beforeAll(async () => {
  await wasm.init();
  // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
  const createModule = await import('../coverwise.js');
  raw = (await createModule.default()) as RawModule;
});

describe('the tuple-explosion diagnostic', () => {
  for (const { name, model } of models) {
    it(`reports ${name} the same way on every surface`, () => {
      const fromCore = raw.generate(model());
      expect(fromCore.error).toBe(true);
      expect(fromCore.message).toBe('t-wise tuple count exceeds safety limit');

      for (const { name: surface, api } of packages) {
        const refusal = refusalOf(api, model());
        expect(refusal.code, surface).toBe('TUPLE_EXPLOSION');
        expect(refusal.message, surface).toBe(fromCore.message);
        expect(refusal.detail, surface).toBe(fromCore.detail);
      }
    });

    it(`quotes ${name} as a decimal integer on every surface`, () => {
      // A rounded figure is still an integer, so the digits themselves are what
      // the assertion above compares. What is asserted here is that no surface
      // leaves the notation the others are read in: an exponent, or an
      // `Infinity`, is not a count a caller can parse or compare.
      const figure = (detail: string | undefined) => detail?.match(/Total tuples: (\S+),/)?.[1];

      expect(figure(raw.generate(model()).detail)).toMatch(/^[0-9]+$/);
      for (const { name: surface, api } of packages) {
        expect(figure(refusalOf(api, model()).detail), surface).toMatch(/^[0-9]+$/);
      }
    });
  }
});
