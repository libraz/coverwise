/// A parameter is a boundary parameter only when it owns a `type`, `range` or
/// `step` field. The input validator and the WASM binding both test own fields,
/// so the pure surface must too: a field reached through the prototype chain
/// would otherwise expand boundary values there and nowhere else.

import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from './index.js';
import * as pure from './pure/index.js';
import type { GenerateInput, Parameter } from './types.js';

beforeAll(async () => {
  await wasm.init();
});

describe('boundary detection reads own fields only', () => {
  it('ignores boundary fields inherited from a prototype on both surfaces', () => {
    const inherited = Object.create({ type: 'integer', range: [0, 10] }) as Parameter;
    Object.assign(inherited, { name: 'n', values: ['a', 'b'] });
    const input: GenerateInput = {
      parameters: [inherited, { name: 'm', values: ['x', 'y'] }],
      seed: 1,
    };

    const fromWasm = wasm.generate(input);
    const fromPure = pure.generate(input);
    const values = (result: typeof fromWasm) => [...new Set(result.tests.map((t) => t.n))].sort();
    expect(values(fromWasm)).toEqual(['a', 'b']);
    expect(values(fromPure)).toEqual(values(fromWasm));
  });
});
