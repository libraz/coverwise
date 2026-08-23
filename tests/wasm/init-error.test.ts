import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterEach, describe, expect, it, vi } from 'vitest';

/**
 * The specifier js/index.ts uses for the WASM glue resolves, through the vitest
 * alias, to this absolute path. Mocking it here intercepts the same module the
 * library imports.
 */
const wasmModulePath = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../dist/coverwise.js',
);

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

/**
 * A failed `init()` clears the cached promise so a later call retries. Without
 * that reset the first failure would be cached forever and every later `init()`
 * would reject with it, even once the cause was gone.
 */
describe('init() after a failed attempt', () => {
  afterEach(() => {
    vi.doUnmock(wasmModulePath);
    vi.resetModules();
  });

  it('rejects the first attempt and succeeds on the retry', async () => {
    await vi.resetModules();

    let attempts = 0;
    const fakeModule = {
      generate: () => ({ tests: [], coverage: 1 }),
      analyzeCoverage: () => ({ totalTuples: 0 }),
      extendTests: () => ({ tests: [] }),
      estimateModel: () => ({ totalTuples: 0 }),
    };
    vi.doMock(wasmModulePath, () => ({
      default: async () => {
        attempts += 1;
        if (attempts === 1) {
          throw new Error('instantiation refused');
        }
        return fakeModule;
      },
    }));

    const mod = await import('../../js/index');

    await expect(mod.init()).rejects.toBeInstanceOf(mod.CoverwiseError);
    expect(attempts).toBe(1);

    // The retry must reach the module again rather than replay the cached
    // rejection, and must leave the singleton usable.
    await expect(mod.init()).resolves.toBeUndefined();
    expect(attempts).toBe(2);
    expect(mod.generate({ parameters: [{ name: 'a', values: ['0', '1'] }], strength: 1 })).toEqual({
      tests: [],
      coverage: 1,
      negativeTests: [],
    });
  });

  it('reports the first failure as a CoverwiseError naming the cause', async () => {
    await vi.resetModules();

    vi.doMock(wasmModulePath, () => ({
      default: async () => {
        throw new Error('instantiation refused');
      },
    }));

    const mod = await import('../../js/index');

    let thrown: unknown;
    try {
      await mod.init();
    } catch (e) {
      thrown = e;
    }

    expect(thrown).toBeInstanceOf(mod.CoverwiseError);
    expect((thrown as InstanceType<typeof mod.CoverwiseError>).code).toBe('INVALID_INPUT');
    expect((thrown as Error).message).toContain('instantiation refused');
  });
});
