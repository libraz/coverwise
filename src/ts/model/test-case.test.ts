import { describe, expect, it } from 'vitest';
import {
  createGenerateResult,
  createGenerateStats,
  createUncoveredTuple,
  uncoveredTupleToString,
} from './test-case.js';

describe('createUncoveredTuple', () => {
  it('creates a default uncovered tuple', () => {
    const ut = createUncoveredTuple();
    expect(ut.tuple).toEqual([]);
    expect(ut.params).toEqual([]);
    expect(ut.reason).toBe('never covered');
  });

  it('creates an uncovered tuple with custom values', () => {
    const ut = createUncoveredTuple(['os=win', 'browser=safari'], ['os', 'browser'], 'skipped');
    expect(ut.tuple).toEqual(['os=win', 'browser=safari']);
    expect(ut.params).toEqual(['os', 'browser']);
    expect(ut.reason).toBe('skipped');
  });
});

describe('uncoveredTupleToString', () => {
  it('formats a tuple as comma-separated string', () => {
    const ut = createUncoveredTuple(['os=win', 'browser=safari'], ['os', 'browser']);
    expect(uncoveredTupleToString(ut)).toBe('os=win, browser=safari');
  });

  it('handles single-element tuple', () => {
    const ut = createUncoveredTuple(['os=win'], ['os']);
    expect(uncoveredTupleToString(ut)).toBe('os=win');
  });

  it('handles empty tuple', () => {
    const ut = createUncoveredTuple();
    expect(uncoveredTupleToString(ut)).toBe('');
  });
});

describe('createGenerateStats', () => {
  it('creates stats with default zeros', () => {
    const stats = createGenerateStats();
    expect(stats.totalTuples).toBe(0);
    expect(stats.coveredTuples).toBe(0);
    expect(stats.testCount).toBe(0);
  });
});

describe('createGenerateResult', () => {
  it('creates a result with default empty arrays and zero coverage', () => {
    const result = createGenerateResult();
    expect(result.tests).toEqual([]);
    expect(result.negativeTests).toEqual([]);
    expect(result.coverage).toBe(0);
    expect(result.uncovered).toEqual([]);
    expect(result.stats.totalTuples).toBe(0);
    expect(result.stats.coveredTuples).toBe(0);
    expect(result.stats.testCount).toBe(0);
    expect(result.suggestions).toEqual([]);
    expect(result.warnings).toEqual([]);
  });
});
