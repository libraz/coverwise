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

  it('marks generated values outside the range invalid', () => {
    const integer = expandBoundaryValues(new Parameter('n', []), {
      type: BoundaryType.Integer,
      minValue: 1,
      maxValue: 10,
      step: 1,
    });
    expect(integer.values).toEqual(['0', '1', '2', '9', '10', '11']);
    expect(integer.invalid).toEqual([true, false, false, false, false, true]);

    const float = expandBoundaryValues(new Parameter('ratio', []), {
      type: BoundaryType.Float,
      minValue: 0.1,
      maxValue: 0.3,
      step: 0.1,
    });
    expect(float.values).toEqual(['0', '0.1', '0.2', '0.3', '0.4']);
    expect(float.invalid).toEqual([true, false, false, false, true]);
  });

  it('keeps the metadata of a declared value outside the range', () => {
    // The caller listed 0 explicitly without marking it invalid, so it stays valid.
    const result = expandBoundaryValues(new Parameter('n', ['0']), {
      type: BoundaryType.Integer,
      minValue: 1,
      maxValue: 10,
      step: 1,
    });
    expect(result.isInvalid(resolveValueName(result, '0'))).toBe(false);
    expect(result.isInvalid(resolveValueName(result, '11'))).toBe(true);
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
    // -1 lies below the range, so the generated value is a negative-test input.
    expect(result.isInvalid(generated)).toBe(true);
    expect(result.isInvalid(resolveValueName(result, '0'))).toBe(false);
    expect(result.aliases(generated)).toEqual([]);
    expect(result.equivalenceClass(generated)).toBe('');
  });
});
