/// Error-shape and input-validation parity tests.
///
/// Every public surface — the WASM-backed default export and the pure
/// TypeScript export — must throw a `CoverwiseError` (an `instanceof Error`
/// carrying a typed string `code`) for the same bad input, and must never let a
/// malformed parameter abort the WASM module.

import { beforeAll, describe, expect, it } from 'vitest';
import { analyzeCoverage, CoverwiseError, extendTests, generate, init } from './index.js';
import {
  analyzeCoverage as pureAnalyzeCoverage,
  extendTests as pureExtendTests,
  generate as pureGenerate,
} from './pure/index.js';
import type { GenerateInput, Parameter, TestCase } from './types.js';

interface Surface {
  name: string;
  generate: (input: GenerateInput) => unknown;
  analyzeCoverage: (
    parameters: Parameter[],
    tests: TestCase[],
    strength?: number,
    constraints?: string[],
  ) => unknown;
  extendTests: (existing: TestCase[], input: GenerateInput) => unknown;
}

const surfaces: Surface[] = [
  { name: 'wasm', generate, analyzeCoverage, extendTests },
  {
    name: 'pure',
    generate: pureGenerate,
    analyzeCoverage: pureAnalyzeCoverage,
    extendTests: pureExtendTests,
  },
];

const okParams: Parameter[] = [
  { name: 'os', values: ['win', 'mac', 'linux'] },
  { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
];

/** Run `fn`, returning the thrown error (fails if it does not throw). */
function capture(fn: () => unknown): unknown {
  try {
    fn();
  } catch (e) {
    return e;
  }
  throw new Error('expected function to throw');
}

describe('error shape and input validation', () => {
  beforeAll(async () => {
    await init();
  });

  // --- Unified error shape ---

  describe('unified error shape', () => {
    for (const surface of surfaces) {
      describe(surface.name, () => {
        it('throws CoverwiseError (instanceof Error) with CONSTRAINT_ERROR for a bad constraint', () => {
          const err = capture(() =>
            surface.generate({
              parameters: okParams,
              // Unknown parameter referenced in the constraint.
              constraints: ['IF nope = mac THEN browser != ie'],
            }),
          );
          expect(err).toBeInstanceOf(Error);
          expect(err).toBeInstanceOf(CoverwiseError);
          expect((err as CoverwiseError).code).toBe('CONSTRAINT_ERROR');
          expect((err as CoverwiseError).message).toBeTruthy();
          expect((err as CoverwiseError).detail).toContain('Available parameters:');
        });

        it('throws CoverwiseError with INVALID_INPUT for invalid input', () => {
          const err = capture(() =>
            surface.generate({ parameters: 'not-an-array' as unknown as Parameter[] }),
          );
          expect(err).toBeInstanceOf(CoverwiseError);
          expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
        });
      });
    }

    it('both surfaces agree on the code for a bad constraint', () => {
      const input: GenerateInput = {
        parameters: okParams,
        constraints: ['IF nope = mac THEN browser != ie'],
      };
      const wasmErr = capture(() => generate(input)) as CoverwiseError;
      const pureErr = capture(() => pureGenerate(input)) as CoverwiseError;
      expect(wasmErr.code).toBe(pureErr.code);
      expect(wasmErr.code).toBe('CONSTRAINT_ERROR');
    });

    it('both surfaces agree on the code for invalid input', () => {
      const input = {
        parameters: [{ name: 'os', values: ['win', 'win'] }],
      } as GenerateInput;
      const wasmErr = capture(() => generate(input)) as CoverwiseError;
      const pureErr = capture(() => pureGenerate(input)) as CoverwiseError;
      expect(wasmErr.code).toBe(pureErr.code);
      expect(wasmErr.code).toBe('INVALID_INPUT');
    });
  });

  // --- Hardened input validation ---

  describe('input validation', () => {
    // Each case is a parameter array that must be rejected with INVALID_INPUT.
    const badParameterCases: Array<{ label: string; params: unknown }> = [
      // A dropped row leaves a hole in the array itself, not just in `values`.
      // Reading `.name` off it without a shape check would surface as a raw
      // TypeError from inside the library instead of a structured rejection.
      { label: 'a null parameter entry', params: [null] },
      { label: 'an undefined parameter entry', params: [undefined] },
      { label: 'an array where a parameter object belongs', params: [['os', 'win']] },
      { label: 'missing values', params: [{ name: 'os' }] },
      { label: 'string values', params: [{ name: 'os', values: 'win' }] },
      { label: 'empty values array', params: [{ name: 'os', values: [] }] },
      { label: 'empty name', params: [{ name: '', values: ['win'] }] },
      { label: 'duplicate value', params: [{ name: 'os', values: ['win', 'win'] }] },
      {
        label: 'duplicate parameter name',
        params: [
          { name: 'os', values: ['win'] },
          { name: 'os', values: ['mac'] },
        ],
      },
    ];

    for (const surface of surfaces) {
      describe(surface.name, () => {
        for (const { label, params } of badParameterCases) {
          it(`rejects ${label} with a descriptive INVALID_INPUT error`, () => {
            const err = capture(() => surface.generate({ parameters: params as Parameter[] }));
            expect(err).toBeInstanceOf(CoverwiseError);
            expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
            expect((err as CoverwiseError).message.length).toBeGreaterThan(0);
          });
        }

        it('rejects a non-array tests argument to analyzeCoverage', () => {
          const err = capture(() =>
            surface.analyzeCoverage(okParams, 'nope' as unknown as TestCase[]),
          );
          expect(err).toBeInstanceOf(CoverwiseError);
          expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
        });

        it('rejects a non-array existing argument to extendTests', () => {
          const err = capture(() =>
            surface.extendTests('nope' as unknown as TestCase[], { parameters: okParams }),
          );
          expect(err).toBeInstanceOf(CoverwiseError);
          expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
        });
      });
    }

    it('C++ and TS report identical messages for each bad parameter case', () => {
      for (const { params } of badParameterCases) {
        const wasmErr = capture(() =>
          generate({ parameters: params as Parameter[] }),
        ) as CoverwiseError;
        const pureErr = capture(() =>
          pureGenerate({ parameters: params as Parameter[] }),
        ) as CoverwiseError;
        expect(wasmErr.message).toBe(pureErr.message);
      }
    });

    // The critical WASM-specific invariant: a malformed parameter must NOT abort
    // the module (which would poison the singleton). A later valid call must
    // still succeed.
    it('does not abort the WASM module on bad input (subsequent valid call works)', () => {
      expect(() => generate({ parameters: [{ name: 'os' } as Parameter] })).toThrow();
      const result = generate({ parameters: okParams, seed: 1 }) as {
        coverage: number;
      };
      expect(result.coverage).toBe(1.0);
    });

    const malformedNestedInputs: Array<{ label: string; input: GenerateInput }> = [
      {
        label: 'constraints is not a string array',
        input: { parameters: okParams, constraints: 'A=a' as unknown as string[] },
      },
      {
        label: 'seed is missing a parameter',
        input: { parameters: okParams, seeds: [{ os: 'win' }] },
      },
      {
        label: 'ParameterValue aliases is not an array',
        input: {
          parameters: [
            { name: 'os', values: [{ value: 'win', aliases: 'windows' as unknown as string[] }] },
            okParams[1],
          ],
        },
      },
      {
        label: 'subModels has a malformed parameter list',
        input: {
          parameters: okParams,
          subModels: [{ parameters: 'os' as unknown as string[], strength: 1 }],
        },
      },
      {
        label: 'weight is non-finite',
        input: { parameters: okParams, weights: { os: { win: Number.NaN } } },
      },
      {
        label: 'boundary step is non-positive',
        input: {
          parameters: [
            { name: 'ratio', values: ['0.5'], type: 'float', range: [0, 1], step: 0 },
            okParams[1],
          ],
        },
      },
    ];

    for (const { label, input } of malformedNestedInputs) {
      it(`both surfaces reject ${label} with CoverwiseError`, () => {
        for (const surface of surfaces) {
          const err = capture(() => surface.generate(input));
          expect(err).toBeInstanceOf(CoverwiseError);
          expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
        }
      });
    }

    it('both surfaces use CoverwiseError for invalid numeric scalars', () => {
      for (const surface of surfaces) {
        const err = capture(() => surface.generate({ parameters: okParams, seed: -1 }));
        expect(err).toBeInstanceOf(CoverwiseError);
        expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
      }
    });
  });

  // A `values` array with a hole (a dropped row from a YAML conversion or a
  // `rows.map(r => r.value)` over sparse data) must be reported through the
  // structured error channel, naming the parameter and the offending index —
  // never as a raw TypeError from an unguarded property read.
  describe('holes in a values array', () => {
    const holes: Array<{ label: string; hole: unknown }> = [
      { label: 'null', hole: null },
      { label: 'undefined', hole: undefined },
    ];

    /** Assert `err` is the structured error naming `os[1]`. */
    function expectStructured(err: unknown): void {
      expect(err).toBeInstanceOf(CoverwiseError);
      expect((err as CoverwiseError).code).toBe('INVALID_INPUT');
      expect((err as CoverwiseError).message).toContain('os[1]');
    }

    for (const { label, hole } of holes) {
      const parameters = [
        { name: 'os', values: ['win', hole] as unknown as string[] },
        okParams[1],
      ];

      for (const surface of surfaces) {
        it(`${surface.name} generate reports a ${label} value as INVALID_INPUT`, () => {
          expectStructured(capture(() => surface.generate({ parameters })));
        });

        it(`${surface.name} analyzeCoverage reports a ${label} value as INVALID_INPUT`, () => {
          expectStructured(
            capture(() => surface.analyzeCoverage(parameters, [{ os: 'win', browser: 'chrome' }])),
          );
        });

        it(`${surface.name} extendTests reports a ${label} value as INVALID_INPUT`, () => {
          expectStructured(
            capture(() => surface.extendTests([{ os: 'win', browser: 'chrome' }], { parameters })),
          );
        });
      }

      it(`both surfaces report a ${label} value identically`, () => {
        const wasmErr = capture(() => generate({ parameters })) as CoverwiseError;
        const pureErr = capture(() => pureGenerate({ parameters })) as CoverwiseError;
        expect(wasmErr.message).toBe(pureErr.message);
        expect(wasmErr.code).toBe(pureErr.code);
      });
    }
  });
});
