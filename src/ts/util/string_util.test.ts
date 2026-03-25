import { isNumeric, toDouble } from './string_util.js';

describe('isNumeric', () => {
  it('returns true for integer strings', () => {
    expect(isNumeric('123')).toBe(true);
    expect(isNumeric('0')).toBe(true);
    expect(isNumeric('999999')).toBe(true);
  });

  it('returns true for decimal strings', () => {
    expect(isNumeric('3.14')).toBe(true);
    expect(isNumeric('0.5')).toBe(true);
    expect(isNumeric('.5')).toBe(true);
  });

  it('returns true for negative numbers', () => {
    expect(isNumeric('-5')).toBe(true);
    expect(isNumeric('-3.14')).toBe(true);
    expect(isNumeric('-0')).toBe(true);
  });

  it('returns true for scientific notation', () => {
    expect(isNumeric('1e10')).toBe(true);
    expect(isNumeric('2.5e-3')).toBe(true);
  });

  it('returns false for empty string', () => {
    expect(isNumeric('')).toBe(false);
  });

  it('returns false for non-numeric strings', () => {
    expect(isNumeric('abc')).toBe(false);
    expect(isNumeric('12abc')).toBe(false);
    expect(isNumeric('hello')).toBe(false);
  });

  it('returns false for whitespace-only strings', () => {
    expect(isNumeric(' ')).toBe(false);
    expect(isNumeric('  ')).toBe(false);
    expect(isNumeric('\t')).toBe(false);
  });

  it('returns false for Infinity', () => {
    expect(isNumeric('Infinity')).toBe(false);
    expect(isNumeric('-Infinity')).toBe(false);
  });

  it('returns false for NaN', () => {
    expect(isNumeric('NaN')).toBe(false);
  });
});

describe('toDouble', () => {
  it('parses integers', () => {
    expect(toDouble('42')).toBe(42);
    expect(toDouble('0')).toBe(0);
  });

  it('parses decimals', () => {
    expect(toDouble('3.14')).toBeCloseTo(3.14);
  });

  it('parses negative numbers', () => {
    expect(toDouble('-1')).toBe(-1);
    expect(toDouble('-0.5')).toBe(-0.5);
  });

  it('parses scientific notation', () => {
    expect(toDouble('1e3')).toBe(1000);
  });
});
