import { describe, expect, it, vi } from 'vitest';

/**
 * Verifies that the init/getModule failure path throws a `CoverwiseError`
 * (instanceof Error, with a `.code`) rather than a plain `Error`, so callers
 * can branch on the error type uniformly across the whole public surface.
 *
 * Uses an isolated module registry so the WASM singleton is NOT initialized:
 * calling a public API before `init()` must surface a `CoverwiseError`.
 */
describe('uninitialized module error', () => {
  it('throws CoverwiseError(INVALID_INPUT) when used before init()', async () => {
    await vi.resetModules();
    const mod = await import('../../js/index');

    let thrown: unknown;
    try {
      mod.generate({
        parameters: [
          { name: 'a', values: ['0', '1'] },
          { name: 'b', values: ['0', '1'] },
        ],
      });
    } catch (e) {
      thrown = e;
    }

    expect(thrown).toBeInstanceOf(mod.CoverwiseError);
    expect(thrown).toBeInstanceOf(Error);
    expect((thrown as InstanceType<typeof mod.CoverwiseError>).code).toBe('INVALID_INPUT');
  });
});
