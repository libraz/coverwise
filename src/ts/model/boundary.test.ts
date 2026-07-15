import { describe, expect, it } from 'vitest';

import { BoundaryType, expandBoundaryValues } from './boundary.js';
import { Parameter } from './parameter.js';

describe('expandBoundaryValues', () => {
  it('preserves spelling and metadata by numeric value identity', () => {
    const param = new Parameter('score', ['1.0', 'other'], [true, false]);
    param.setAliases([['one'], ['fallback']]);
    param.setEquivalenceClasses(['numeric', 'text']);

    const result = expandBoundaryValues(param, {
      type: BoundaryType.Integer,
      minValue: 0,
      maxValue: 2,
      step: 1,
    });

    const one = result.findValueIndex('1.0');
    expect(result.values[one]).toBe('1.0');
    expect(result.isInvalid(one)).toBe(true);
    expect(result.aliases(one)).toEqual(['one']);
    expect(result.equivalenceClass(one)).toBe('numeric');
    const generated = result.findValueIndex('-1');
    expect(result.isInvalid(generated)).toBe(false);
    expect(result.aliases(generated)).toEqual([]);
    expect(result.equivalenceClass(generated)).toBe('');
  });
});
