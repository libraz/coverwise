import { decodeMixedRadix, encodeMixedRadix, generateCombinations } from './combinatorics.js';

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
