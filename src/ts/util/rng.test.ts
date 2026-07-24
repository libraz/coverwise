import { Rng } from './rng.js';

describe('Rng', () => {
  describe('determinism', () => {
    it('same seed produces the same sequence', () => {
      const a = new Rng(42);
      const b = new Rng(42);

      const seqA = Array.from({ length: 100 }, () => a.uniformInt(0, 1000));
      const seqB = Array.from({ length: 100 }, () => b.uniformInt(0, 1000));

      expect(seqA).toEqual(seqB);
    });

    it('different seeds produce different sequences', () => {
      const a = new Rng(1);
      const b = new Rng(2);

      const seqA = Array.from({ length: 20 }, () => a.uniformInt(0, 1000000));
      const seqB = Array.from({ length: 20 }, () => b.uniformInt(0, 1000000));

      expect(seqA).not.toEqual(seqB);
    });
  });

  describe('uniformInt', () => {
    it('returns values in [min, max] inclusive', () => {
      const rng = new Rng(123);
      const min = 10;
      const max = 20;

      for (let i = 0; i < 1000; i++) {
        const v = rng.uniformInt(min, max);
        expect(v).toBeGreaterThanOrEqual(min);
        expect(v).toBeLessThanOrEqual(max);
      }
    });

    it('returns min when min equals max', () => {
      const rng = new Rng(0);
      expect(rng.uniformInt(5, 5)).toBe(5);
      expect(rng.uniformInt(0, 0)).toBe(0);
      expect(rng.uniformInt(-3, -3)).toBe(-3);
    });

    it('returns min when min > max', () => {
      const rng = new Rng(0);
      expect(rng.uniformInt(10, 5)).toBe(10);
    });

    it('covers the full range over many calls', () => {
      const rng = new Rng(99);
      const seen = new Set<number>();

      for (let i = 0; i < 1000; i++) {
        seen.add(rng.uniformInt(0, 4));
      }

      expect(seen.size).toBe(5); // 0, 1, 2, 3, 4
    });
  });

  describe('nextUint32', () => {
    it('returns values in [0, max) exclusive', () => {
      const rng = new Rng(42);

      for (let i = 0; i < 1000; i++) {
        const v = rng.nextUint32(10);
        expect(v).toBeGreaterThanOrEqual(0);
        expect(v).toBeLessThan(10);
      }
    });

    it('returns 0 when max is 0', () => {
      const rng = new Rng(0);
      expect(rng.nextUint32(0)).toBe(0);
    });

    it('returns 0 when max is negative', () => {
      const rng = new Rng(0);
      expect(rng.nextUint32(-5)).toBe(0);
    });

    it('returns 0 when max is 1', () => {
      const rng = new Rng(0);
      // [0, 1) can only be 0
      for (let i = 0; i < 10; i++) {
        expect(rng.nextUint32(1)).toBe(0);
      }
    });
  });

  // Pins the xoshiro128** + SplitMix32 integer stream to an absolute golden
  // sequence. The identical expected values are asserted from the C++ side in
  // tests/util/rng_test.cpp (RngTest.MatchesTypeScriptReferenceStream), so the
  // two surfaces independently lock to one stream: if either implementation
  // drifts, its own golden test fails. This is what guarantees byte-identical
  // generation across the C++/WASM and TypeScript surfaces.
  describe('cross-surface golden stream', () => {
    it('matches the pinned reference stream shared with C++', () => {
      const cases: Array<{ seed: number; max: number; expected: number[] }> = [
        { seed: 42, max: 1000, expected: [924, 897, 282, 142, 180, 290, 186, 688, 214, 302] },
        { seed: 123, max: 10, expected: [1, 5, 9, 0, 4, 8, 4, 3, 0, 9] },
        { seed: 7, max: 100, expected: [0, 87, 49, 28, 27, 69, 77, 14] },
      ];
      for (const { seed, max, expected } of cases) {
        const rng = new Rng(seed);
        const actual = expected.map(() => rng.nextUint32(max));
        expect(actual, `seed=${seed}`).toEqual(expected);
      }
    });

    // The low-32-bit seed truncation must match C++ (static_cast<uint32_t>).
    it('uses only the low 32 bits of the seed', () => {
      const a = new Rng(42);
      const b = new Rng((42 + 2 ** 32) >>> 0);
      const seqA = Array.from({ length: 50 }, () => a.nextUint32(1000));
      const seqB = Array.from({ length: 50 }, () => b.nextUint32(1000));
      expect(seqA).toEqual(seqB);
    });
  });

  describe('seed', () => {
    it('reseeding produces the same sequence as a fresh instance', () => {
      const rng = new Rng(100);
      // Advance the state
      for (let i = 0; i < 50; i++) {
        rng.uniformInt(0, 100);
      }

      // Reseed
      rng.seed(42);
      const fresh = new Rng(42);

      const seqReseeded = Array.from({ length: 50 }, () => rng.uniformInt(0, 1000));
      const seqFresh = Array.from({ length: 50 }, () => fresh.uniformInt(0, 1000));

      expect(seqReseeded).toEqual(seqFresh);
    });
  });

  describe('stress', () => {
    it('does not crash after a large number of calls', () => {
      const rng = new Rng(0);
      for (let i = 0; i < 100000; i++) {
        rng.uniformInt(0, 1000);
      }
      // If we get here without error, the test passes.
      expect(true).toBe(true);
    });
  });

  describe('surface', () => {
    it('does not expose the removed weightedRandomIndex helper', () => {
      const rng = new Rng(0) as unknown as Record<string, unknown>;
      expect(rng.weightedRandomIndex).toBeUndefined();
    });
  });
});
