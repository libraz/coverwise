/// Tests for the shared input-validation module (js/validation.ts).
///
/// The WASM-backed and pure surfaces both delegate to these helpers; this file
/// pins the shared behavior directly and confirms the two surfaces still reject
/// the same bad scalar inputs after de-duplication.

import { beforeAll, describe, expect, it } from 'vitest';
import { aggregateBudgetExceeded } from '../src/ts/model/budget.js';
import { generate, init } from './index.js';
import { generate as pureGenerate } from './pure/index.js';
import type { GenerateInput } from './types.js';
import { CoverwiseError } from './types.js';
import {
  createStringBudget,
  type ScalarErrorFactory,
  validateConstraints,
  validateGenerateInput,
  validateMaxTests,
  validateParameters,
  validateSeed,
  validateStrength,
  validateTestArray,
} from './validation.js';

const makeError: ScalarErrorFactory = (message) => new Error(message);

describe('shared validation module', () => {
  describe('validateStrength', () => {
    it('defaults undefined/null to 2', () => {
      expect(validateStrength(undefined, makeError)).toBe(2);
      expect(validateStrength(null, makeError)).toBe(2);
    });

    it('accepts a positive integer', () => {
      expect(validateStrength(3, makeError)).toBe(3);
    });

    it('rejects non-positive, non-integer, and non-number values', () => {
      expect(() => validateStrength(0, makeError)).toThrow(/Invalid strength/);
      expect(() => validateStrength(-1, makeError)).toThrow(/Invalid strength/);
      expect(() => validateStrength(1.5, makeError)).toThrow(/Invalid strength/);
      expect(() => validateStrength('2', makeError)).toThrow(/Invalid strength/);
    });

    it('rejects a value beyond the unsigned 32-bit range', () => {
      expect(validateStrength(0xffffffff, makeError)).toBe(0xffffffff);
      expect(() => validateStrength(2 ** 32, makeError)).toThrow(/Invalid strength/);
    });
  });

  describe('validateMaxTests', () => {
    it('accepts undefined/null and non-negative integers', () => {
      expect(() => validateMaxTests(undefined, makeError)).not.toThrow();
      expect(() => validateMaxTests(null, makeError)).not.toThrow();
      expect(() => validateMaxTests(0, makeError)).not.toThrow();
      expect(() => validateMaxTests(10, makeError)).not.toThrow();
    });

    it('rejects negative, non-integer, and non-number values', () => {
      expect(() => validateMaxTests(-1, makeError)).toThrow(/Invalid maxTests/);
      expect(() => validateMaxTests(2.5, makeError)).toThrow(/Invalid maxTests/);
      expect(() => validateMaxTests('5', makeError)).toThrow(/Invalid maxTests/);
    });
  });

  describe('validateSeed', () => {
    it('accepts undefined/null and the full uint32 range', () => {
      expect(() => validateSeed(undefined, makeError)).not.toThrow();
      expect(() => validateSeed(null, makeError)).not.toThrow();
      expect(() => validateSeed(0, makeError)).not.toThrow();
      expect(() => validateSeed(0xffffffff, makeError)).not.toThrow();
    });

    it('rejects out-of-range, non-integer, and non-number values', () => {
      expect(() => validateSeed(-1, makeError)).toThrow(/Invalid seed/);
      expect(() => validateSeed(0x100000000, makeError)).toThrow(/Invalid seed/);
      expect(() => validateSeed(1.5, makeError)).toThrow(/Invalid seed/);
      expect(() => validateSeed('0', makeError)).toThrow(/Invalid seed/);
    });
  });

  describe('validateParameters / validateTestArray', () => {
    it('throws CoverwiseError(INVALID_INPUT) for non-arrays', () => {
      const paramErr = capture(() => validateParameters('nope'));
      expect(paramErr).toBeInstanceOf(CoverwiseError);
      expect((paramErr as CoverwiseError).code).toBe('INVALID_INPUT');

      const testErr = capture(() => validateTestArray('nope', 'tests'));
      expect(testErr).toBeInstanceOf(CoverwiseError);
      expect((testErr as CoverwiseError).code).toBe('INVALID_INPUT');
    });

    it('accepts arrays', () => {
      expect(() => validateParameters([])).not.toThrow();
      expect(() => validateTestArray([], 'tests')).not.toThrow();
    });
  });

  describe('validateGenerateInput', () => {
    it('runs all structural checks and surfaces the first failure', () => {
      const bad = { parameters: [], strength: 0 } as unknown as GenerateInput;
      expect(() => validateGenerateInput(bad, makeError)).toThrow(/Invalid strength/);
    });

    it('rejects aggregate UTF-8 string data above the admission budget', () => {
      const oversized = 'x'.repeat(64 * 1024);
      const input = {
        parameters: [
          { name: 'a', values: [oversized] },
          { name: 'b', values: [oversized] },
        ],
        constraints: Array.from({ length: 15 }, () => oversized),
      } as unknown as GenerateInput;
      expect(() => validateGenerateInput(input, makeError)).toThrow(aggregateBudgetExceeded());
    });
  });
});

// --- Surface parity: the de-duplicated validators still gate both surfaces ---

describe('surface validation parity', () => {
  beforeAll(async () => {
    await init();
  });

  const okParams = [{ name: 'os', values: ['win', 'mac', 'linux'] }];

  const badScalarCases: Array<{ label: string; input: GenerateInput; match: RegExp }> = [
    { label: 'strength', input: { parameters: okParams, strength: 0 }, match: /Invalid strength/ },
    { label: 'maxTests', input: { parameters: okParams, maxTests: -1 }, match: /Invalid maxTests/ },
    { label: 'seed', input: { parameters: okParams, seed: -1 }, match: /Invalid seed/ },
  ];

  // The scalar validators take their error type from a factory the entry point
  // injects, which is the one place the two surfaces could report the same
  // refusal as different things. Both inject the same one, so a caller sees a
  // CoverwiseError carrying INVALID_INPUT either way, and the injection point
  // is a seam for tests and embedders rather than a difference to branch on.
  for (const { label, input, match } of badScalarCases) {
    it(`both surfaces reject invalid ${label} as the same error`, () => {
      const thrown = [capture(() => generate(input)), capture(() => pureGenerate(input))];
      for (const [index, error] of thrown.entries()) {
        const surface = index === 0 ? 'wasm' : 'pure';
        expect(error, surface).toBeInstanceOf(CoverwiseError);
        expect((error as CoverwiseError).code, surface).toBe('INVALID_INPUT');
        expect((error as CoverwiseError).message, surface).toMatch(match);
      }
      expect((thrown[0] as CoverwiseError).message).toBe((thrown[1] as CoverwiseError).message);
      expect((thrown[0] as CoverwiseError).detail).toBe((thrown[1] as CoverwiseError).detail);
    });
  }
});

describe('caller-data reader and budget charging', () => {
  it('charges a numeric or boolean parameter value as its rendered text', () => {
    const budget = createStringBudget();
    validateParameters([{ name: 'p', values: [12345, true, { value: 0.5 }] }], budget);
    expect(budget.value).toBe('p'.length + '12345'.length + 'true'.length + '0.5'.length);
    expect(budget.rows).toBe(0);
  });

  it('charges only row strings, and tracks them apart from the model', () => {
    const budget = createStringBudget();
    validateTestArray([{ p: 'abc', q: 12345, r: true }], 'tests', budget);
    expect(budget.value).toBe(3);
    expect(budget.rows).toBe(3);
  });

  it('accepts a row of any width', () => {
    const row: Record<string, number> = {};
    for (let i = 0; i < 2000; ++i) {
      row[`k${i}`] = i;
    }
    expect(() => validateTestArray([row], 'tests')).not.toThrow();
  });

  it('reports a hole in an array field as invalid input', () => {
    const aliases = new Array<string>(2);
    aliases[0] = 'x';
    expect(() =>
      validateParameters([{ name: 'p', values: [{ value: 'a', aliases }] }] as never),
    ).toThrow(/Aliases at p\[0\]/);
    expect(() => validateConstraints(new Array<string>(1))).toThrow(/array of strings/);
  });
});

/** Run `fn`, returning the thrown error (fails if it does not throw). */
function capture(fn: () => unknown): unknown {
  try {
    fn();
  } catch (e) {
    return e;
  }
  throw new Error('expected function to throw');
}
