import {
  asciiCaseInsensitiveEqual,
  asciiToUpper,
  isNumeric,
  jsNumberToString,
  toDouble,
} from './string_util.js';

// Shared accept/reject corpus. The C++ `IsNumeric` test asserts the exact same
// expectations so the two surfaces agree token-for-token.
const NUMERIC_CORPUS: ReadonlyArray<[string, boolean]> = [
  ['123', true],
  ['-12.5', true],
  ['+.5', true],
  ['12.', true],
  ['1e9', true],
  ['-3.0E-2', true],
  ['inf', false],
  ['Infinity', false],
  ['nan', false],
  ['0x1f', false],
  [' 5 ', false],
  ['', false],
  ['1.2.3', false],
  ['1,000', false],
];

describe('isNumeric', () => {
  it('matches the shared numeric grammar corpus', () => {
    for (const [input, expected] of NUMERIC_CORPUS) {
      expect(isNumeric(input)).toBe(expected);
    }
  });

  it('returns true for integer strings', () => {
    expect(isNumeric('123')).toBe(true);
    expect(isNumeric('0')).toBe(true);
    expect(isNumeric('999999')).toBe(true);
  });

  it('returns true for decimal strings', () => {
    expect(isNumeric('3.14')).toBe(true);
    expect(isNumeric('0.5')).toBe(true);
    expect(isNumeric('.5')).toBe(true);
    expect(isNumeric('12.')).toBe(true);
  });

  it('returns true for signed numbers', () => {
    expect(isNumeric('-5')).toBe(true);
    expect(isNumeric('-3.14')).toBe(true);
    expect(isNumeric('-0')).toBe(true);
    expect(isNumeric('+5')).toBe(true);
    expect(isNumeric('+.5')).toBe(true);
  });

  it('returns true for scientific notation', () => {
    expect(isNumeric('1e10')).toBe(true);
    expect(isNumeric('2.5e-3')).toBe(true);
    expect(isNumeric('-3.0E-2')).toBe(true);
  });

  it('returns false for empty string', () => {
    expect(isNumeric('')).toBe(false);
  });

  it('returns false for non-numeric strings', () => {
    expect(isNumeric('abc')).toBe(false);
    expect(isNumeric('12abc')).toBe(false);
    expect(isNumeric('hello')).toBe(false);
  });

  it('returns false for whitespace (including padded numbers)', () => {
    expect(isNumeric(' ')).toBe(false);
    expect(isNumeric('  ')).toBe(false);
    expect(isNumeric('\t')).toBe(false);
    expect(isNumeric(' 5 ')).toBe(false);
    expect(isNumeric('5 ')).toBe(false);
    expect(isNumeric(' 5')).toBe(false);
  });

  it('returns false for non-finite tokens', () => {
    expect(isNumeric('Infinity')).toBe(false);
    expect(isNumeric('-Infinity')).toBe(false);
    expect(isNumeric('inf')).toBe(false);
    expect(isNumeric('NaN')).toBe(false);
    expect(isNumeric('nan')).toBe(false);
  });

  it('returns false for hex, multiple dots, and thousands separators', () => {
    expect(isNumeric('0x1f')).toBe(false);
    expect(isNumeric('1.2.3')).toBe(false);
    expect(isNumeric('1,000')).toBe(false);
    expect(isNumeric('1e')).toBe(false);
    expect(isNumeric('.')).toBe(false);
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

// Byte-equality corpus shared with the C++ JsNumberToString test. The C++
// formatter reproduces JavaScript's Number-to-String algorithm, so both
// surfaces must produce these exact strings for each value.
const JS_NUMBER_CORPUS: ReadonlyArray<[number, string]> = [
  [3.14, '3.14'],
  [0.1, '0.1'],
  [1 / 3, '0.3333333333333333'],
  [0.1 + 0.2, '0.30000000000000004'],
  [2.5, '2.5'],
  [-0.0, '0'],
  [0.0, '0'],
  [100.0, '100'],
  [1e-7, '1e-7'],
  [42.0, '42'],
  [-42.0, '-42'],
  [1e21, '1e+21'],
  [1e-21, '1e-21'],
  [1e-6, '0.000001'],
  [1e20, '100000000000000000000'],
  [-3.14, '-3.14'],
  [123456789.0, '123456789'],
];

describe('jsNumberToString', () => {
  it('matches String(value) on the shared cross-surface corpus', () => {
    for (const [value, expected] of JS_NUMBER_CORPUS) {
      expect(jsNumberToString(value)).toBe(expected);
    }
  });
});

describe('asciiToUpper', () => {
  it('folds ASCII letters only', () => {
    expect(asciiToUpper('abc')).toBe('ABC');
    expect(asciiToUpper('aBcZ_9')).toBe('ABCZ_9');
  });

  it('leaves non-ASCII characters untouched', () => {
    // 'é' (U+00E9) must not fold to 'É' (U+00C9).
    expect(asciiToUpper('café')).toBe('CAFé');
    expect(asciiToUpper('Ä')).toBe('Ä');
  });
});

describe('asciiCaseInsensitiveEqual', () => {
  it('treats ASCII case differences as equal', () => {
    expect(asciiCaseInsensitiveEqual('Os', 'os')).toBe(true);
    expect(asciiCaseInsensitiveEqual('CHROME', 'chrome')).toBe(true);
  });

  it('does NOT treat non-ASCII case differences as equal', () => {
    // Full-Unicode folding would call these equal; ASCII-only must not.
    expect(asciiCaseInsensitiveEqual('é', 'É')).toBe(false);
    expect(asciiCaseInsensitiveEqual('café', 'CAFÉ')).toBe(false);
    expect(asciiCaseInsensitiveEqual('straße', 'STRASSE')).toBe(false);
  });

  it('still matches the ASCII portion of mixed strings', () => {
    expect(asciiCaseInsensitiveEqual('caFé', 'CAFé')).toBe(true);
  });

  it('returns false for different lengths', () => {
    expect(asciiCaseInsensitiveEqual('ab', 'abc')).toBe(false);
  });
});
