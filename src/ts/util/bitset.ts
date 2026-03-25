/// @file bitset.ts
/// @brief Dynamic bitset for coverage tracking using Uint32Array (32-bit blocks).

const BITS_PER_BLOCK = 32;

function numBlocks(numBits: number): number {
  return ((numBits + BITS_PER_BLOCK - 1) / BITS_PER_BLOCK) | 0;
}

/// Hamming weight (popcount) for a 32-bit integer.
function popcount32(x: number): number {
  x = x - ((x >>> 1) & 0x55555555);
  x = (x & 0x33333333) + ((x >>> 2) & 0x33333333);
  x = (x + (x >>> 4)) & 0x0f0f0f0f;
  return (x * 0x01010101) >>> 24;
}

/// A dynamic bitset optimized for coverage tracking.
///
/// Uses Uint32Array blocks for efficient bulk operations (AND, OR, popcount).
export class DynamicBitset {
  private numBits: number;
  private blocks: Uint32Array;

  /// Construct a bitset with the given number of bits, all cleared.
  constructor(numBits: number = 0) {
    this.numBits = numBits;
    this.blocks = new Uint32Array(numBlocks(numBits));
  }

  /// Set the bit at the given index.
  set(index: number): void {
    this.blocks[index >>> 5] |= (1 << (index & 31)) >>> 0;
  }

  /// Clear the bit at the given index.
  clear(index: number): void {
    this.blocks[index >>> 5] &= ~((1 << (index & 31)) >>> 0);
  }

  /// Test whether the bit at the given index is set.
  test(index: number): boolean {
    return (this.blocks[index >>> 5] & ((1 << (index & 31)) >>> 0)) !== 0;
  }

  /// Count the number of set bits.
  count(): number {
    let total = 0;
    for (let i = 0; i < this.blocks.length; i++) {
      total += popcount32(this.blocks[i]);
    }
    return total;
  }

  /// Return the total number of bits.
  size(): number {
    return this.numBits;
  }

  /// Count bits set in (this AND NOT other).
  /// If other is shorter, missing blocks are treated as 0.
  countAndNot(other: DynamicBitset): number {
    let total = 0;
    const otherLen = other.blocks.length;
    for (let i = 0; i < this.blocks.length; i++) {
      const otherBlock = i < otherLen ? other.blocks[i] : 0;
      total += popcount32((this.blocks[i] & ~otherBlock) >>> 0);
    }
    return total;
  }

  /// Set all bits from other into this (this |= other).
  unionWith(other: DynamicBitset): void {
    for (let i = 0; i < this.blocks.length; i++) {
      this.blocks[i] |= other.blocks[i];
    }
  }

  /// Clear all bits.
  reset(): void {
    this.blocks.fill(0);
  }
}
