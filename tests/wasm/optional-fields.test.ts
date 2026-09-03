import { beforeAll, describe, expect, it } from 'vitest';
import { type GenerateInput, init, generate as wasmGenerate } from '../../js/index';
import { generate as pureGenerate } from '../../js/pure/index';

/// An optional field the caller did not fill in is `undefined` in JavaScript,
/// and `null` once the same input has been through a JSON round trip. Neither
/// says anything more than "absent", so both have to produce the documented
/// default — and produce it on the WASM surface and the pure one alike, since a
/// caller choosing between them is promised the same answers.
///
/// `{ parameters, strength: config.strength }` is the shape that makes this
/// matter: the key is written, so it exists, and its value is whatever the
/// caller's own config held.

const NOT_SUPPLIED = [
  { label: 'undefined', value: undefined },
  { label: 'null', value: null },
] as const;

/// Built fresh per case: the two runs being compared must not share objects, or
/// a mutation by either surface would hide the difference being measured.
function model(): GenerateInput {
  return {
    parameters: [
      { name: 'os', values: ['win', 'mac', 'linux'] },
      { name: 'browser', values: ['chrome', 'firefox'] },
    ],
  };
}

/// Every optional field of GenerateInput, each with a value that leaves the
/// result identical to omitting the field, so "absent" and "explicitly nothing"
/// can be compared directly.
const OPTIONAL_FIELDS = [
  'strength',
  'seed',
  'maxTests',
  'constraints',
  'seeds',
  'weights',
  'subModels',
] as const;

describe('optional fields holding undefined or null', () => {
  beforeAll(async () => {
    await init();
  });

  for (const field of OPTIONAL_FIELDS) {
    for (const { label, value } of NOT_SUPPLIED) {
      it(`treats ${field}: ${label} as absent on both surfaces`, () => {
        const supplied = { ...model(), [field]: value } as GenerateInput;

        let wasmResult: ReturnType<typeof wasmGenerate> | undefined;
        expect(() => {
          wasmResult = wasmGenerate(supplied);
        }).not.toThrow();
        const pureResult = pureGenerate(supplied);

        // The documented default for strength is 2, and it is the one default
        // the result reports back, so it stands for "the defaults were applied"
        // whichever field is being nulled out.
        expect(wasmResult?.strength).toBe(2);
        expect(pureResult.strength).toBe(2);
        expect(wasmResult?.coverage).toBe(1);

        // Identical to the same input with the key removed entirely.
        expect(wasmResult?.tests).toEqual(wasmGenerate(model()).tests);
        expect(pureResult.tests).toEqual(pureGenerate(model()).tests);
      });
    }
  }

  // A value object carries its own optional members. The package entry points
  // decide for themselves what a `null` one means, so only `undefined` is a
  // question for these two surfaces; the binding's own reading of both is
  // asserted against the compiled module directly.

  it('treats aliases: undefined on a value as absent on both surfaces', () => {
    const withField = {
      parameters: [
        { name: 'browser', values: [{ value: 'chromium', aliases: undefined }, 'firefox'] },
        { name: 'os', values: ['win', 'mac'] },
      ],
    } as unknown as GenerateInput;
    const without = {
      parameters: [
        { name: 'browser', values: [{ value: 'chromium' }, 'firefox'] },
        { name: 'os', values: ['win', 'mac'] },
      ],
    } as unknown as GenerateInput;

    expect(wasmGenerate(withField).tests).toEqual(wasmGenerate(without).tests);
    expect(pureGenerate(withField).tests).toEqual(pureGenerate(without).tests);
  });

  it('treats class: undefined on a value as absent on both surfaces', () => {
    const withField = {
      parameters: [
        { name: 'browser', values: [{ value: 'chromium', class: undefined }, 'firefox'] },
        { name: 'os', values: ['win', 'mac'] },
      ],
    } as unknown as GenerateInput;
    const without = {
      parameters: [
        { name: 'browser', values: [{ value: 'chromium' }, 'firefox'] },
        { name: 'os', values: ['win', 'mac'] },
      ],
    } as unknown as GenerateInput;

    const wasmResult = wasmGenerate(withField);
    // No class was described, so none is reported: a missing member must not
    // become an equivalence class of its own.
    expect(wasmResult.classCoverage).toBeUndefined();
    expect(wasmResult.tests).toEqual(wasmGenerate(without).tests);
    expect(pureGenerate(withField).tests).toEqual(pureGenerate(without).tests);
  });
});
