import { beforeAll, describe, expect, it } from 'vitest';

/// A sub-model's strength is a required field on every surface: the published
/// type declares it, the package wrapper and the CLI refuse a sub-model without
/// it. The compiled module is reachable without the wrapper, so it must refuse
/// the same input rather than quietly substitute a default strength.

interface RawResult {
  error?: true;
  code?: number;
  message?: string;
}

interface RawModule {
  generate(input: unknown): RawResult;
}

const INVALID_INPUT = 3;

let raw: RawModule;

describe('the compiled module reading sub-models', () => {
  beforeAll(async () => {
    // @ts-expect-error — built by `yarn build:wasm`, aliased to dist/ in vitest.
    const createModule = await import('../coverwise.js');
    raw = (await createModule.default()) as RawModule;
  });

  it('refuses a sub-model without a strength', () => {
    const result = raw.generate({
      parameters: [
        { name: 'a', values: ['0', '1'] },
        { name: 'b', values: ['0', '1'] },
        { name: 'c', values: ['0', '1'] },
      ],
      subModels: [{ parameters: ['a', 'b'] }],
    });
    expect(result.error).toBe(true);
    expect(result.code).toBe(INVALID_INPUT);
    expect(result.message).toBe('Invalid subModels[0].');
  });
});
