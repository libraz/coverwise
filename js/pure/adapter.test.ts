import { describe, expect, it } from 'vitest';
import { Parameter } from '../../src/ts/model/parameter.js';
import { toInternalOptions } from './adapter.js';

function parameterWithValueReadCounter(): { parameter: Parameter; reads: number } {
  const parameter = new Parameter('p', ['a', 'b', 'c']);
  let reads = 0;
  const values = new Proxy(parameter.values, {
    get(target, property, receiver) {
      if (typeof property === 'string' && /^(0|[1-9]\d*)$/.test(property)) {
        reads += 1;
      }
      return Reflect.get(target, property, receiver);
    },
  });
  Object.defineProperty(parameter, 'values', { get: () => values });
  return {
    parameter,
    get reads() {
      return reads;
    },
  };
}

describe('toInternalOptions row conversion', () => {
  it('does not scan parameter values when seeds are absent or empty', () => {
    for (const input of [{ parameters: [] }, { parameters: [], seeds: [] }]) {
      const counted = parameterWithValueReadCounter();

      toInternalOptions(input, [counted.parameter]);

      expect(counted.reads).toBe(0);
    }
  });

  it('builds a resolver when a seed needs conversion', () => {
    const counted = parameterWithValueReadCounter();

    const options = toInternalOptions({ parameters: [], seeds: [{ p: 'b' }] }, [counted.parameter]);

    expect(options.seeds[0].values).toEqual([1]);
    expect(counted.reads).toBe(3);
  });
});
