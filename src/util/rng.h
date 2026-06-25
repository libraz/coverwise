/// @file rng.h
/// @brief Seeded random number generator for deterministic generation.

#ifndef COVERWISE_UTIL_RNG_H_
#define COVERWISE_UTIL_RNG_H_

#include <cstdint>

namespace coverwise {
namespace util {

/// @brief Seeded RNG using the xoshiro128** algorithm with SplitMix32 seeding.
///
/// The state is initialized from a single 32-bit seed via SplitMix32. Integer
/// sampling uses rejection sampling to avoid modulo bias. This implementation
/// is byte-for-byte identical to the TypeScript reference in
/// `src/ts/util/rng.ts`, so the same seed produces the same sequence on both
/// the native/WASM (C++) and JavaScript/TypeScript surfaces.
class Rng {
 public:
  /// @brief Construct a PRNG with the given seed.
  ///
  /// Only the low 32 bits of the seed are used; this is the canonical seed
  /// domain shared with the TypeScript surface.
  explicit Rng(uint64_t seed = 0) { Seed(seed); }

  /// @brief Generate a random uint32_t in [0, max).
  /// @param max Exclusive upper bound. Returns 0 when max is 0.
  uint32_t NextUint32(uint32_t max);

  /// @brief Reseed the generator from the low 32 bits of seed.
  void Seed(uint64_t seed);

 private:
  /// @brief Generate the next raw 32-bit value using xoshiro128**.
  uint32_t Next();

  uint32_t s0_ = 0;
  uint32_t s1_ = 0;
  uint32_t s2_ = 0;
  uint32_t s3_ = 0;
};

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_RNG_H_
