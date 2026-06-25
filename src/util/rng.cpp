/// @file rng.cpp

#include "util/rng.h"

namespace coverwise {
namespace util {

namespace {

/// @brief 32-bit left rotation.
inline uint32_t Rotl(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }

}  // namespace

void Rng::Seed(uint64_t seed) {
  // SplitMix32: expand a single 32-bit seed into the four 32-bit state words.
  // All arithmetic is performed modulo 2^32 (native uint32_t wraparound),
  // mirroring JavaScript's Math.imul / `>>> 0` semantics.
  uint32_t state = static_cast<uint32_t>(seed);
  uint32_t words[4];
  for (int i = 0; i < 4; ++i) {
    state += 0x9e3779b9u;
    uint32_t z = state;
    z = (z ^ (z >> 16)) * 0x85ebca6bu;
    z = (z ^ (z >> 13)) * 0xc2b2ae35u;
    words[i] = z ^ (z >> 16);
  }
  s0_ = words[0];
  s1_ = words[1];
  s2_ = words[2];
  s3_ = words[3];
  // Ensure state is never all-zero.
  if ((s0_ | s1_ | s2_ | s3_) == 0) {
    s0_ = 1;
  }
}

uint32_t Rng::Next() {
  const uint32_t result = Rotl(s1_ * 5u, 7) * 9u;
  const uint32_t t = s1_ << 9;

  s2_ ^= s0_;
  s3_ ^= s1_;
  s1_ ^= s2_;
  s0_ ^= s3_;

  s2_ ^= t;
  s3_ = Rotl(s3_, 11);

  return result;
}

uint32_t Rng::NextUint32(uint32_t max) {
  if (max == 0) return 0;
  // Mirrors uniformInt(0, max - 1). When max == 1 the range is a single value,
  // so the result is 0 without consuming a draw (matching the TS short-circuit
  // `min >= max` in uniformInt). This keeps the integer stream identical.
  if (max == 1) return 0;
  // range == max.
  // Rejection sampling to avoid modulo bias.
  // threshold == (2^32 - range) % range == (-range) % range.
  const uint32_t range = max;
  const uint32_t threshold = (0u - range) % range;
  uint32_t r;
  do {
    r = Next();
  } while (r < threshold);
  return r % range;
}

}  // namespace util
}  // namespace coverwise
