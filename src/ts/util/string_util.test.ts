import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  bitsToHex,
  doubleToBits,
  NUMERIC_PARSE_CORPUS,
} from '../../../tests/util/numeric-parse-corpus.js';
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

  // The corpus is shared with the C++ test, which reads the same file. Every
  // surface has to land on the same bit pattern for these decimals, so a model
  // using a subnormal produces one suite rather than one per platform.
  it('matches the shared decimal corpus bit for bit', () => {
    for (const numericCase of NUMERIC_PARSE_CORPUS) {
      expect(isNumeric(numericCase.text)).toBe(true);
      expect(bitsToHex(doubleToBits(toDouble(numericCase.text))), numericCase.text).toBe(
        bitsToHex(numericCase.bits),
      );
      // The corpus claims to record Number(text); hold it to that claim.
      expect(bitsToHex(doubleToBits(Number(numericCase.text))), numericCase.text).toBe(
        bitsToHex(numericCase.bits),
      );
    }
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
  // Subnormal and extreme magnitudes.
  [5e-324, '5e-324'], // Number.MIN_VALUE (subnormal)
  [2.2250738585072014e-308, '2.2250738585072014e-308'], // smallest normal
  [1.7976931348623157e308, '1.7976931348623157e+308'], // Number.MAX_VALUE
  // Safe-integer boundary.
  [9007199254740991, '9007199254740991'], // MAX_SAFE_INTEGER
  [9007199254740992, '9007199254740992'],
  // Exponential thresholds (n === 22 and n === -7 tip into scientific form).
  [1e22, '1e+22'],
  [5e-7, '5e-7'],
  // Rounding that carries into a new magnitude: shortest round-trip is 1e+23.
  [9.999999999999999e22, '1e+23'],
  // Decimal-form boundaries around n in (-6, 0].
  [0.0001, '0.0001'],
  [0.00001, '0.00001'],
  [0.5, '0.5'],
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

/**
 * A primitive that decides something the whole engine has to agree about, and
 * the module that is allowed to define it.
 *
 * Nothing in either language makes a second definition impossible to write, and
 * a second one is how these primitives regrow: two copies answer the same
 * question and drift apart, and the surfaces built on them disagree without
 * anything failing. This is the mechanism that keeps each one single.
 */
interface SharedPrimitive {
  /** What the primitive decides, phrased as the test name reads. */
  readonly what: string;
  /** The module allowed to define it, per language that has one. */
  readonly definedIn: readonly string[];
  /**
   * Ways of writing the primitive's own work -- the step that only its
   * definition has reason to perform, not the ordinary operations built on it.
   */
  readonly motifs: readonly RegExp[];
  /**
   * Files carrying a definition of their own, asserted exactly rather than
   * merely permitted: a new one fails, and so does an entry that has stopped
   * being true, which is what stops the list from outliving its contents.
   */
  readonly knownDuplicates: readonly string[];
}

const SHARED_PRIMITIVES: readonly SharedPrimitive[] = [
  {
    what: 'the ASCII case fold',
    definedIn: ['src/util/string_util.cpp', 'src/ts/util/string_util.ts'],
    // Keyed on the mapping, not on the letter ranges: testing whether a byte is
    // a letter is an ordinary thing for a tokenizer to do, and only the shift
    // that turns one case into the other is a fold.
    motifs: [/'a' - 'A'/, /'A' - 'a'/, /toLowerCase/, /toUpperCase/, /0x20/],
    knownDuplicates: [],
  },
  {
    what: 'the UTF-8 byte count',
    // C++ has no counterpart: a std::string already knows its byte length, so
    // only the TypeScript surfaces need one.
    definedIn: ['src/ts/model/utf8.ts'],
    // Keyed on accumulating a byte total, which is the counting itself.
    // Decoding a string into codepoints is a different operation and several
    // modules legitimately do it.
    motifs: [/bytes \+=/, /\.byteLength/],
    knownDuplicates: [],
  },
];

/**
 * The walk covers the wrapper in `js/` as well as the engine, because these
 * defects cross that boundary: a wrapper that folds or counts on its own can
 * reject a model the engine accepts. It lives in this tier rather than the C++
 * one because it is a check on source trees, not on compiled symbols.
 */
const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

const SCANNED_TREES = ['src', 'js'];

function scannableSources(directory: string): string[] {
  const found: string[] = [];
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      found.push(...scannableSources(full));
    } else if (/\.(cpp|h|ts)$/.test(entry.name) && !entry.name.endsWith('.test.ts')) {
      found.push(full);
    }
  }
  return found;
}

describe('shared primitives have one definition each', () => {
  const sources = SCANNED_TREES.flatMap((tree) => scannableSources(path.join(REPO_ROOT, tree))).map(
    (file) => path.relative(REPO_ROOT, file),
  );

  it('finds the sources to scan', () => {
    expect(sources.length).toBeGreaterThan(10);
    for (const tree of SCANNED_TREES) {
      expect(sources.some((file) => file.startsWith(`${tree}${path.sep}`))).toBe(true);
    }
  });

  for (const primitive of SHARED_PRIMITIVES) {
    const carriers = sources
      .filter((file) => !primitive.definedIn.includes(file))
      .filter((file) => {
        const source = readFileSync(path.join(REPO_ROOT, file), 'utf8');
        return primitive.motifs.some((motif) => motif.test(source));
      })
      .sort();

    it(`defines ${primitive.what} in exactly the files that should`, () => {
      expect(carriers).toEqual(primitive.knownDuplicates.map((f) => path.normalize(f)));
    });

    it(`finds ${primitive.what} inside the module that owns it`, () => {
      for (const owner of primitive.definedIn) {
        const source = readFileSync(path.join(REPO_ROOT, owner), 'utf8');
        expect({ [owner]: primitive.motifs.some((motif) => motif.test(source)) }).toEqual({
          [owner]: true,
        });
      }
    });
  }
});
