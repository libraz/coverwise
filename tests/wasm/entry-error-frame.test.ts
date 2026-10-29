import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { afterEach, beforeAll, describe, expect, it, vi } from 'vitest';
import * as wasm from '../../js/index.js';
import * as pure from '../../js/pure/index.js';
import type { Parameter, TestCase, WeightConfig } from '../../js/types.js';
import { CoverwiseError } from '../../js/types.js';

/// Every public entry point of both npm entries runs inside one error frame:
/// whatever is thrown while it works leaves as a CoverwiseError, and the wording
/// of the error says which side failed — a caller's property read or the engine.

const CALLER_THREW = 'the caller refused the read';
const PROPERTY_READ = 'a property read on the input threw';

const model = (): Parameter[] => [
  { name: 'n', values: ['a', 'b'] },
  { name: 'm', values: ['x', 'y'] },
];
const row = (): TestCase => ({ n: 'a', m: 'x' });
const weights = (): WeightConfig => ({ n: { a: 2 } });

/** A copy of `base` whose `key` throws when read, as a getter on a store object does. */
function throwingGetter<T extends object>(base: T, key: string): T {
  return Object.defineProperty({ ...base }, key, {
    enumerable: true,
    configurable: true,
    get: () => {
      throw new Error(CALLER_THREW);
    },
  });
}

/** `base` behind a proxy whose `get` trap throws for `key`. */
function throwingProxy<T extends object>(base: T, key: string): T {
  return new Proxy(base, {
    get: (target, property, receiver) => {
      if (property === key) {
        throw new Error(CALLER_THREW);
      }
      return Reflect.get(target, property, receiver);
    },
  });
}

const surfaces = [
  { name: 'wasm', api: wasm },
  { name: 'pure', api: pure },
] as const;

const hazards = [
  { name: 'a getter', plant: throwingGetter },
  { name: 'a proxy trap', plant: throwingProxy },
] as const;

function caught(call: () => unknown): unknown {
  try {
    call();
  } catch (thrown) {
    return thrown;
  }
  return undefined;
}

beforeAll(async () => {
  await wasm.init();
});

describe('caller code that throws during a property read', () => {
  for (const { name: surface, api } of surfaces) {
    for (const { name: hazard, plant } of hazards) {
      const calls: Array<[string, () => unknown]> = [
        ['generate', () => api.generate(plant({ parameters: model() }, 'parameters'))],
        [
          'generate weights',
          () => api.generate(plant({ parameters: model(), weights: weights() }, 'weights')),
        ],
        ['analyzeCoverage', () => api.analyzeCoverage(model(), [plant(row(), 'n')])],
        [
          'extendTests',
          () => api.extendTests([row()], plant({ parameters: model() }, 'parameters')),
        ],
        ['estimateModel', () => api.estimateModel(plant({ parameters: model() }, 'parameters'))],
        [
          'the class API',
          () => {
            const input = plant({ parameters: model() }, 'parameters');
            return api.Coverwise.create().then((cw) => cw.generate(input));
          },
        ],
      ];

      for (const [entry, call] of calls) {
        it(`${surface}.${entry} reports ${hazard} as an INVALID_INPUT CoverwiseError`, async () => {
          const thrown = await Promise.resolve()
            .then(call)
            .then(
              () => undefined,
              (error: unknown) => error,
            );
          expect(thrown).toBeInstanceOf(CoverwiseError);
          expect((thrown as CoverwiseError).code).toBe('INVALID_INPUT');
          expect((thrown as CoverwiseError).message).toContain(CALLER_THREW);
        });
      }
    }
  }
});

describe('a failure of the engine itself', () => {
  afterEach(() => {
    vi.doUnmock(
      path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../dist/coverwise.js'),
    );
    vi.doUnmock('../../src/ts/core/generator.js');
    vi.resetModules();
  });

  it('is not reported as a property read when the pure engine throws', async () => {
    await vi.resetModules();
    vi.doMock('../../src/ts/core/generator.js', () => ({
      generate: () => {
        throw new TypeError('engine bug');
      },
      extend: () => {
        throw new TypeError('engine bug');
      },
      estimateModel: () => {
        throw new TypeError('engine bug');
      },
    }));
    const mod = await import('../../js/pure/index.js');

    for (const call of [
      () => mod.generate({ parameters: model() }),
      () => mod.extendTests([row()], { parameters: model() }),
      () => mod.estimateModel({ parameters: model() }),
    ]) {
      const thrown = caught(call);
      expect(thrown).toBeInstanceOf(mod.CoverwiseError);
      expect((thrown as CoverwiseError).code).toBe('INVALID_INPUT');
      expect((thrown as CoverwiseError).message).toContain('engine bug');
      expect((thrown as CoverwiseError).message).not.toContain(PROPERTY_READ);
    }
  });

  it('is not reported as a property read when the WASM module traps, and init() reloads it', async () => {
    await vi.resetModules();
    const wasmModulePath = path.resolve(
      path.dirname(fileURLToPath(import.meta.url)),
      '../../dist/coverwise.js',
    );
    let loads = 0;
    const healthy = {
      generate: () => ({ tests: [], coverage: 1 }),
      analyzeCoverage: () => ({ totalTuples: 0 }),
      extendTests: () => ({ tests: [] }),
      estimateModel: () => ({ totalTuples: 0 }),
    };
    const trapping = {
      ...healthy,
      generate: () => {
        throw new WebAssembly.RuntimeError('unreachable executed');
      },
    };
    vi.doMock(wasmModulePath, () => ({
      default: async () => {
        loads += 1;
        return loads === 1 ? trapping : healthy;
      },
    }));
    const mod = await import('../../js/index.js');
    await mod.init();

    const trapped = caught(() => mod.generate({ parameters: model() }));
    expect(trapped).toBeInstanceOf(mod.CoverwiseError);
    expect((trapped as CoverwiseError).message).toContain('unreachable executed');
    expect((trapped as CoverwiseError).message).not.toContain(PROPERTY_READ);

    // The module that trapped is not treated as initialized any longer.
    const unready = caught(() => mod.generate({ parameters: model() }));
    expect((unready as CoverwiseError).message).toContain('not initialized');

    await mod.init();
    expect(loads).toBe(2);
    // The reloaded module, not the trapped one, answers.
    expect(mod.generate({ parameters: model() })).toMatchObject(healthy.generate());
  });
});
