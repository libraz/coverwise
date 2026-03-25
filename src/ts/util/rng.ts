/// @file rng.ts
/// @brief Seeded PRNG for deterministic generation using xoshiro128**.

/// SplitMix32: used to initialize xoshiro128** state from a single seed.
function splitmix32(seed: number): () => number {
  let state = seed >>> 0;
  return (): number => {
    state = (state + 0x9e3779b9) >>> 0;
    let z = state;
    z = Math.imul(z ^ (z >>> 16), 0x85ebca6b) >>> 0;
    z = Math.imul(z ^ (z >>> 13), 0xc2b2ae35) >>> 0;
    return (z ^ (z >>> 16)) >>> 0;
  };
}

/// Seeded PRNG using xoshiro128** algorithm.
///
/// Provides deterministic random number generation: same seed always produces
/// the same sequence of outputs.
export class Rng {
  private s0: number;
  private s1: number;
  private s2: number;
  private s3: number;

  /// Construct a PRNG with the given seed.
  constructor(seed: number = 0) {
    const init = splitmix32(seed);
    this.s0 = init();
    this.s1 = init();
    this.s2 = init();
    this.s3 = init();
    // Ensure state is never all-zero.
    if ((this.s0 | this.s1 | this.s2 | this.s3) === 0) {
      this.s0 = 1;
    }
  }

  /// Generate the next raw 32-bit value using xoshiro128**.
  private next(): number {
    const result = Math.imul(rotl(Math.imul(this.s1, 5) >>> 0, 7), 9) >>> 0;
    const t = (this.s1 << 9) >>> 0;

    this.s2 ^= this.s0;
    this.s3 ^= this.s1;
    this.s1 ^= this.s2;
    this.s0 ^= this.s3;

    this.s2 ^= t;
    this.s3 = rotl(this.s3, 11);

    // Keep all state as unsigned 32-bit.
    this.s0 >>>= 0;
    this.s1 >>>= 0;
    this.s2 >>>= 0;
    this.s3 >>>= 0;

    return result;
  }

  /// Generate a random integer in [min, max] (inclusive).
  uniformInt(min: number, max: number): number {
    if (min >= max) {
      return min;
    }
    const range = (max - min + 1) >>> 0;
    // Use rejection sampling to avoid modulo bias.
    // Standard debiasing: threshold = (2^32 - range) % range = (-range) % range.
    // Values below threshold map unevenly; reject them.
    const threshold = ((-range >>> 0) % range) >>> 0;
    let r: number;
    do {
      r = this.next();
    } while (r < threshold);
    return min + (r % range);
  }

  /// Generate a random uint32 in [0, max) (exclusive upper bound).
  nextUint32(max: number): number {
    if (max <= 0) {
      return 0;
    }
    return this.uniformInt(0, max - 1);
  }

  /// Select a random index weighted by the given weights array.
  ///
  /// @param weights Array of non-negative weights. At least one must be positive.
  /// @returns The selected index.
  weightedRandomIndex(weights: readonly number[]): number {
    let totalWeight = 0;
    for (let i = 0; i < weights.length; i++) {
      totalWeight += weights[i];
    }
    if (totalWeight <= 0) {
      return this.nextUint32(weights.length);
    }
    // Generate a random value in [0, totalWeight).
    // Use floating-point multiplication for uniform distribution across the range.
    const threshold = ((this.next() >>> 0) / 0x100000000) * totalWeight;
    let cumulative = 0;
    for (let i = 0; i < weights.length; i++) {
      cumulative += weights[i];
      if (threshold < cumulative) {
        return i;
      }
    }
    // Fallback for floating-point edge cases.
    return weights.length - 1;
  }

  /// Reseed the generator.
  seed(seed: number): void {
    const init = splitmix32(seed);
    this.s0 = init();
    this.s1 = init();
    this.s2 = init();
    this.s3 = init();
    if ((this.s0 | this.s1 | this.s2 | this.s3) === 0) {
      this.s0 = 1;
    }
  }
}

/// 32-bit left rotation.
function rotl(x: number, k: number): number {
  return ((x << k) | (x >>> (32 - k))) >>> 0;
}
