import { beforeAll, describe, expect, it } from 'vitest';
import { CoverwiseError, generate, init } from '../../js/index.js';
import { NUMERIC_PARSE_CORPUS } from '../util/numeric-parse-corpus.js';

/// The corpus is shared with the C++ and pure-TypeScript tests, which read the
/// same file. Running it through the WASM build checks the engine as it is
/// actually shipped: the standard library backend it compiles against differs
/// from the one a native macOS build gets, and a decimal must still be accepted
/// or rejected the same way on both.
describe('coverwise WASM decimal parsing', () => {
  beforeAll(async () => {
    await init();
  });

  // A tautology keeps every combination valid whatever the literal is, so the
  // only thing that can fail generation is the literal itself.
  const tautology = (literal: string) => `n >= ${literal} OR n < ${literal}`;

  const parameters = [
    { name: 'n', values: ['0', '1'] },
    { name: 'flag', values: ['a', 'b'] },
  ];

  it('classifies every decimal in the shared corpus', () => {
    for (const numericCase of NUMERIC_PARSE_CORPUS) {
      const run = () =>
        generate({ parameters, constraints: [tautology(numericCase.text)], seed: 1 });
      if (numericCase.acceptsLiteral) {
        expect(() => run(), numericCase.text).not.toThrow();
      } else {
        let thrown: unknown;
        try {
          run();
        } catch (error) {
          thrown = error;
        }
        expect(thrown, numericCase.text).toBeInstanceOf(CoverwiseError);
        expect((thrown as CoverwiseError).code, numericCase.text).toBe('CONSTRAINT_ERROR');
        expect((thrown as CoverwiseError).message, numericCase.text).toContain(
          'out-of-range decimal literal',
        );
      }
    }
  });

  it('keeps subnormal values apart from zero and from each other', () => {
    // Distinct subnormals collapsing onto zero would make these two values one
    // numeric identity, and the constraint would no longer separate them.
    const result = generate({
      parameters: [
        { name: 'n', values: ['0', '5e-324', '1e-310'] },
        { name: 'flag', values: ['a', 'b'] },
      ],
      constraints: ['n > 0 AND n < 1e-310'],
      seed: 1,
    });
    expect(result.tests.length).toBeGreaterThan(0);
    for (const test of result.tests) {
      expect(test.n).toBe('5e-324');
    }
  });
});
