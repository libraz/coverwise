import { describe, expect, it } from 'vitest';

import { BoundaryType, expandBoundaryValues } from './boundary.js';
import { Parameter, resolveValueName } from './parameter.js';

describe('expandBoundaryValues', () => {
  it('de-duplicates float boundaries at the precision the range was written in', () => {
    const expand = (minValue: number, maxValue: number) =>
      expandBoundaryValues(new Parameter('ratio', []), {
        type: BoundaryType.Float,
        minValue,
        maxValue,
        step: 0.1,
      }).values;

    expect(expand(0.1, 0.3)).toEqual(['0', '0.1', '0.2', '0.3', '0.4']);
    expect(expand(0.7, 0.9)).toEqual(['0.6', '0.7', '0.8', '0.9', '1']);
  });

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

    const one = resolveValueName(result, '1.0');
    expect(result.values[one]).toBe('1.0');
    expect(result.isInvalid(one)).toBe(true);
    expect(result.aliases(one)).toEqual(['one']);
    expect(result.equivalenceClass(one)).toBe('numeric');
    const generated = resolveValueName(result, '-1');
    expect(result.isInvalid(generated)).toBe(false);
    expect(result.aliases(generated)).toEqual([]);
    expect(result.equivalenceClass(generated)).toBe('');
  });
});
