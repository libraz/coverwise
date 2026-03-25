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

  describe('weightedRandomIndex', () => {
    it('respects weight distribution statistically', () => {
      const rng = new Rng(12345);
      const weights = [1, 0, 0, 0];
      // Weight is entirely on index 0, so it must always return 0.
      for (let i = 0; i < 100; i++) {
        expect(rng.weightedRandomIndex(weights)).toBe(0);
      }
    });

    it('never returns an index with zero weight when others are positive', () => {
      const rng = new Rng(777);
      const weights = [0, 5, 0, 5];

      for (let i = 0; i < 500; i++) {
        const idx = rng.weightedRandomIndex(weights);
        expect(idx === 1 || idx === 3).toBe(true);
      }
    });

    it('distributes roughly proportional to weights', () => {
      const rng = new Rng(42);
      const weights = [3, 1];
      const counts = [0, 0];
      const iterations = 10000;

      for (let i = 0; i < iterations; i++) {
        counts[rng.weightedRandomIndex(weights)]++;
      }

      // Expect index 0 to be picked roughly 3x more than index 1.
      // Allow wide tolerance for statistical variance.
      const ratio = counts[0] / counts[1];
      expect(ratio).toBeGreaterThan(1.5);
      expect(ratio).toBeLessThan(6.0);
    });

    it('falls back to uniform selection when all weights are zero', () => {
      const rng = new Rng(42);
      const weights = [0, 0, 0, 0];
      const seen = new Set<number>();

      for (let i = 0; i < 1000; i++) {
        const idx = rng.weightedRandomIndex(weights);
        expect(idx).toBeGreaterThanOrEqual(0);
        expect(idx).toBeLessThan(weights.length);
        seen.add(idx);
      }

      // With uniform fallback over 1000 tries, we should see all 4 indices.
      expect(seen.size).toBe(4);
    });

    it('handles a single-element weights array', () => {
      const rng = new Rng(0);
      expect(rng.weightedRandomIndex([5])).toBe(0);
      expect(rng.weightedRandomIndex([0])).toBe(0);
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
});
