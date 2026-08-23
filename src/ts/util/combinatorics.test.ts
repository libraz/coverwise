import { BINOMIAL_CORPUS } from '../../../tests/util/binomial-corpus.js';
import {
  checkedBinomial,
  decodeMixedRadix,
  encodeMixedRadix,
  generateCombinations,
  generateCombinationsFlat,
} from './combinatorics.js';

describe('generateCombinations', () => {
  it('generates C(4,2) = 6 combinations', () => {
    const combos = generateCombinations(4, 2);
    expect(combos).toHaveLength(6);

    const expected = [
      [0, 1],
      [0, 2],
      [0, 3],
      [1, 2],
      [1, 3],
      [2, 3],
    ];
    expect(combos).toEqual(expected);
  });

  it('generates C(5,1) = 5 combinations', () => {
    const combos = generateCombinations(5, 1);
    expect(combos).toHaveLength(5);
    expect(combos).toEqual([[0], [1], [2], [3], [4]]);
  });

  it('generates C(5,5) = 1 combination', () => {
    const combos = generateCombinations(5, 5);
    expect(combos).toHaveLength(1);
    expect(combos).toEqual([[0, 1, 2, 3, 4]]);
  });

  it('returns empty when k is 0', () => {
    expect(generateCombinations(5, 0)).toEqual([]);
  });

  it('returns empty when k > n', () => {
    expect(generateCombinations(3, 5)).toEqual([]);
  });

  it('generates C(1,1) = 1 combination', () => {
    expect(generateCombinations(1, 1)).toEqual([[0]]);
  });

  it('each combination is sorted in ascending order', () => {
    const combos = generateCombinations(6, 3);
    for (const combo of combos) {
      for (let i = 1; i < combo.length; i++) {
        expect(combo[i]).toBeGreaterThan(combo[i - 1]);
      }
    }
  });

  it('generates C(6,3) = 20 combinations', () => {
    const combos = generateCombinations(6, 3);
    expect(combos).toHaveLength(20);
  });

  it('all combinations contain unique elements', () => {
    const combos = generateCombinations(5, 3);
    for (const combo of combos) {
      const unique = new Set(combo);
      expect(unique.size).toBe(combo.length);
    }
  });

  it('no duplicate combinations', () => {
    const combos = generateCombinations(5, 3);
    const serialized = combos.map((c) => c.join(','));
    const unique = new Set(serialized);
    expect(unique.size).toBe(combos.length);
  });
});

describe('generateCombinationsFlat', () => {
  it('matches lexicographic nested order without inner arrays', () => {
    expect(generateCombinationsFlat(4, 2)).toEqual([0, 1, 0, 2, 0, 3, 1, 2, 1, 3, 2, 3]);
  });
});

describe('checkedBinomial', () => {
  // The same corpus the C++ CheckedBinomial test drives. The two guard the same
  // allocation-size checks but bound their arithmetic differently — 64-bit
  // overflow there, the double safe-integer range here — so agreement over every
  // budget a call site passes has to be asserted rather than assumed.
  it.each(BINOMIAL_CORPUS.map((c) => [c.n, c.k, c.limit, c.accepted, c.value] as const))(
    'C(%i, %i) under limit %i',
    (n, k, limit, accepted, value) => {
      const result = checkedBinomial(n, k, limit);
      if (accepted) {
        expect(result).toBe(value);
      } else {
        expect(result).toBeNull();
      }
    },
  );

  it('returns exact small values', () => {
    expect(checkedBinomial(5, 2, 1000)).toBe(10);
    expect(checkedBinomial(6, 3, 1000)).toBe(20);
    expect(checkedBinomial(52, 5, 5_000_000)).toBe(2_598_960);
  });

  it('answers k > n with zero and C(n, n) with one', () => {
    expect(checkedBinomial(3, 5, 1000)).toBe(0);
    expect(checkedBinomial(5, 5, 1000)).toBe(1);
    expect(checkedBinomial(5, 0, 1000)).toBe(1);
  });

  it('accepts a count that lands exactly on the limit and rejects the next one', () => {
    expect(checkedBinomial(5, 2, 10)).toBe(10);
    expect(checkedBinomial(5, 2, 9)).toBeNull();
    expect(checkedBinomial(1000, 2, 1_000_000)).toBe(499_500);
    expect(checkedBinomial(1415, 2, 1_000_000)).toBeNull();
  });

  it('rejects a count that leaves the safe-integer range', () => {
    // The product would need more than 53 bits of mantissa, so the value that
    // came out of the division cannot be trusted to compare against the limit.
    expect(checkedBinomial(4_000_000_000, 2, 0xffffffff)).toBeNull();
    expect(checkedBinomial(200, 100, 0xffffffff)).toBeNull();
  });

  // A limit above 2^32 - 1 is the range where this implementation and the C++
  // core can reach different verdicts. The core cannot be handed one at all —
  // its limit type refuses to be built from anything wider — so asking for one
  // here is a caller mistake, and null would read as "the count exceeded the
  // limit" instead.
  it('throws on a limit the C++ core could not express', () => {
    expect(() => checkedBinomial(5, 2, 0x1_0000_0000)).toThrow(RangeError);
    expect(() => checkedBinomial(5, 2, Number.MAX_VALUE)).toThrow(RangeError);
    expect(() => checkedBinomial(5, 2, Number.POSITIVE_INFINITY)).toThrow(RangeError);
    expect(() => checkedBinomial(5, 2, -1)).toThrow(RangeError);
    expect(() => checkedBinomial(5, 2, 1.5)).toThrow(RangeError);
    expect(() => checkedBinomial(5, 2, Number.NaN)).toThrow(RangeError);
  });

  it('names the accepted range in the message it throws', () => {
    expect(() => checkedBinomial(5, 2, 0x1_0000_0000)).toThrow(/\[0, 4294967295\]/);
  });

  it('accepts the widest limit and a zero limit without throwing', () => {
    expect(checkedBinomial(4_294_967_295, 1, 0xffffffff)).toBe(4_294_967_295);
    expect(checkedBinomial(3, 5, 0)).toBe(0);
  });
});

describe('encodeMixedRadix / decodeMixedRadix', () => {
  describe('round-trip', () => {
    it('encode then decode returns the original indices', () => {
      const radixes = [3, 4, 2];
      const indices = [2, 1, 0];
      const flat = encodeMixedRadix(indices, radixes);
      const decoded = decodeMixedRadix(flat, radixes);
      expect(decoded).toEqual(indices);
    });

    it('round-trips all possible combinations for small radixes', () => {
      const radixes = [2, 3, 2];
      const total = 2 * 3 * 2; // 12

      for (let flat = 0; flat < total; flat++) {
        const indices = decodeMixedRadix(flat, radixes);
        const reEncoded = encodeMixedRadix(indices, radixes);
        expect(reEncoded).toBe(flat);
      }
    });
  });

  describe('known values', () => {
    it('radixes [3,2]: index [1,0] encodes to 2', () => {
      // Position 0 has radix 3, position 1 has radix 2.
      // flat = 1 * 2 + 0 = 2
      expect(encodeMixedRadix([1, 0], [3, 2])).toBe(2);
    });

    it('radixes [3,2]: flat 2 decodes to [1,0]', () => {
      expect(decodeMixedRadix(2, [3, 2])).toEqual([1, 0]);
    });

    it('radixes [2,2]: flat 0 -> [0,0], flat 3 -> [1,1]', () => {
      expect(decodeMixedRadix(0, [2, 2])).toEqual([0, 0]);
      expect(decodeMixedRadix(3, [2, 2])).toEqual([1, 1]);
    });

    it('radixes [3,2,2]: flat 0 -> [0,0,0]', () => {
      expect(decodeMixedRadix(0, [3, 2, 2])).toEqual([0, 0, 0]);
      expect(encodeMixedRadix([0, 0, 0], [3, 2, 2])).toBe(0);
    });

    it('decodes flat indices at or above 2^31 without signed truncation', () => {
      // Regression: an intermediate `| 0` would coerce the running quotient to a
      // signed 32-bit int and corrupt every index once the value reaches 2^31.
      // Use two 100k-radix positions so the quotient after the low digit exceeds
      // 2^31 (3_000_000_000 / 100_000 = 30_000).
      const radixes = [100_000, 100_000];
      const flat = 3_000_000_000; // 30_000 * 100_000 + 0
      expect(decodeMixedRadix(flat, radixes)).toEqual([30_000, 0]);
      expect(encodeMixedRadix([30_000, 0], radixes)).toBe(flat);
    });
  });

  describe('edge cases', () => {
    it('single radix', () => {
      expect(encodeMixedRadix([2], [5])).toBe(2);
      expect(decodeMixedRadix(2, [5])).toEqual([2]);
    });

    it('empty radixes', () => {
      expect(encodeMixedRadix([], [])).toBe(0);
      expect(decodeMixedRadix(0, [])).toEqual([]);
    });

    it('all radixes are 1 (single possible value per position)', () => {
      expect(encodeMixedRadix([0, 0, 0], [1, 1, 1])).toBe(0);
      expect(decodeMixedRadix(0, [1, 1, 1])).toEqual([0, 0, 0]);
    });
  });
});
