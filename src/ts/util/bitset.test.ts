import { DynamicBitset } from './bitset.js';

describe('DynamicBitset', () => {
  describe('constructor', () => {
    it('creates a bitset with 0 bits', () => {
      const bs = new DynamicBitset(0);
      expect(bs.size()).toBe(0);
      expect(bs.count()).toBe(0);
    });

    it('creates a bitset with 1 bit', () => {
      const bs = new DynamicBitset(1);
      expect(bs.size()).toBe(1);
      expect(bs.count()).toBe(0);
    });

    it('creates a bitset with exactly 32 bits (one block)', () => {
      const bs = new DynamicBitset(32);
      expect(bs.size()).toBe(32);
      expect(bs.count()).toBe(0);
    });

    it('creates a bitset with 33 bits (two blocks)', () => {
      const bs = new DynamicBitset(33);
      expect(bs.size()).toBe(33);
      expect(bs.count()).toBe(0);
    });

    it('creates a bitset with 64 bits', () => {
      const bs = new DynamicBitset(64);
      expect(bs.size()).toBe(64);
    });

    it('creates a bitset with 100 bits', () => {
      const bs = new DynamicBitset(100);
      expect(bs.size()).toBe(100);
    });

    it('defaults to 0 bits when no argument is given', () => {
      const bs = new DynamicBitset();
      expect(bs.size()).toBe(0);
    });
  });

  describe('set / test / clear', () => {
    it('sets and tests individual bits', () => {
      const bs = new DynamicBitset(64);
      expect(bs.test(0)).toBe(false);

      bs.set(0);
      expect(bs.test(0)).toBe(true);

      bs.set(15);
      expect(bs.test(15)).toBe(true);
      expect(bs.test(14)).toBe(false);
    });

    it('clears a previously set bit', () => {
      const bs = new DynamicBitset(64);
      bs.set(10);
      expect(bs.test(10)).toBe(true);

      bs.clear(10);
      expect(bs.test(10)).toBe(false);
    });

    it('clearing an already-clear bit is a no-op', () => {
      const bs = new DynamicBitset(64);
      bs.clear(5);
      expect(bs.test(5)).toBe(false);
    });

    it('setting an already-set bit is a no-op', () => {
      const bs = new DynamicBitset(64);
      bs.set(7);
      bs.set(7);
      expect(bs.test(7)).toBe(true);
      expect(bs.count()).toBe(1);
    });
  });

  describe('bits at block boundaries', () => {
    it('handles bit 31 (last bit of first block)', () => {
      const bs = new DynamicBitset(64);
      bs.set(31);
      expect(bs.test(31)).toBe(true);
      expect(bs.test(30)).toBe(false);
      expect(bs.test(32)).toBe(false);
    });

    it('handles bit 32 (first bit of second block)', () => {
      const bs = new DynamicBitset(64);
      bs.set(32);
      expect(bs.test(32)).toBe(true);
      expect(bs.test(31)).toBe(false);
      expect(bs.test(33)).toBe(false);
    });

    it('handles bit 33', () => {
      const bs = new DynamicBitset(64);
      bs.set(33);
      expect(bs.test(33)).toBe(true);
      expect(bs.test(32)).toBe(false);
    });

    it('handles bit 63 (last bit of second block)', () => {
      const bs = new DynamicBitset(64);
      bs.set(63);
      expect(bs.test(63)).toBe(true);
      expect(bs.test(62)).toBe(false);
    });

    it('handles bit 64 in a larger bitset', () => {
      const bs = new DynamicBitset(100);
      bs.set(64);
      expect(bs.test(64)).toBe(true);
      expect(bs.test(63)).toBe(false);
      expect(bs.test(65)).toBe(false);
    });
  });

  describe('count', () => {
    it('returns 0 for an empty bitset', () => {
      const bs = new DynamicBitset(100);
      expect(bs.count()).toBe(0);
    });

    it('counts some set bits', () => {
      const bs = new DynamicBitset(100);
      bs.set(0);
      bs.set(31);
      bs.set(32);
      bs.set(63);
      bs.set(99);
      expect(bs.count()).toBe(5);
    });

    it('counts all bits set', () => {
      const bs = new DynamicBitset(64);
      for (let i = 0; i < 64; i++) {
        bs.set(i);
      }
      expect(bs.count()).toBe(64);
    });

    it('counts correctly across block boundaries', () => {
      const bs = new DynamicBitset(33);
      for (let i = 0; i < 33; i++) {
        bs.set(i);
      }
      expect(bs.count()).toBe(33);
    });
  });

  describe('countAndNot', () => {
    it('returns count of bits in this but not other (overlap)', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);

      a.set(0);
      a.set(1);
      a.set(2);
      b.set(1);
      b.set(2);
      b.set(3);

      // a AND NOT b = {0}
      expect(a.countAndNot(b)).toBe(1);
    });

    it('returns full count when no overlap', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);

      a.set(0);
      a.set(1);
      b.set(32);
      b.set(33);

      expect(a.countAndNot(b)).toBe(2);
    });

    it('returns 0 when this is a subset of other', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);

      a.set(5);
      a.set(10);
      b.set(5);
      b.set(10);
      b.set(15);

      expect(a.countAndNot(b)).toBe(0);
    });

    it('returns 0 for two empty bitsets', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);
      expect(a.countAndNot(b)).toBe(0);
    });
  });

  describe('unionWith', () => {
    it('merges bits from other into this', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);

      a.set(0);
      a.set(31);
      b.set(32);
      b.set(63);

      a.unionWith(b);

      expect(a.test(0)).toBe(true);
      expect(a.test(31)).toBe(true);
      expect(a.test(32)).toBe(true);
      expect(a.test(63)).toBe(true);
      expect(a.count()).toBe(4);
    });

    it('union with empty bitset is a no-op', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);

      a.set(5);
      a.unionWith(b);

      expect(a.count()).toBe(1);
      expect(a.test(5)).toBe(true);
    });

    it('union with overlapping bits does not double count', () => {
      const a = new DynamicBitset(64);
      const b = new DynamicBitset(64);

      a.set(10);
      b.set(10);
      b.set(20);

      a.unionWith(b);
      expect(a.count()).toBe(2);
    });
  });

  describe('reset', () => {
    it('clears all bits', () => {
      const bs = new DynamicBitset(100);
      for (let i = 0; i < 100; i++) {
        bs.set(i);
      }
      expect(bs.count()).toBe(100);

      bs.reset();
      expect(bs.count()).toBe(0);
      expect(bs.test(0)).toBe(false);
      expect(bs.test(99)).toBe(false);
    });

    it('reset on empty bitset is a no-op', () => {
      const bs = new DynamicBitset(64);
      bs.reset();
      expect(bs.count()).toBe(0);
    });
  });

  describe('size', () => {
    it('returns the number of bits', () => {
      expect(new DynamicBitset(0).size()).toBe(0);
      expect(new DynamicBitset(1).size()).toBe(1);
      expect(new DynamicBitset(100).size()).toBe(100);
    });

    it('size is unchanged after set/clear operations', () => {
      const bs = new DynamicBitset(50);
      bs.set(10);
      bs.clear(10);
      expect(bs.size()).toBe(50);
    });
  });
});
