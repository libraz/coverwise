/// @file rng.h
/// @brief Seeded random number generator for deterministic generation.

#ifndef COVERWISE_UTIL_RNG_H_
#define COVERWISE_UTIL_RNG_H_

#include <cstdint>
#include <random>

namespace coverwise {
namespace util {

/// @brief Seeded RNG wrapper for deterministic test generation.
class Rng {
 public:
  explicit Rng(uint64_t seed = 0) : engine_(seed) {}

  /// @brief Generate a random uint32_t in [0, max).
  uint32_t NextUint32(uint32_t max);

  /// @brief Reseed the generator.
  void Seed(uint64_t seed) { engine_.seed(seed); }

 private:
  std::mt19937_64 engine_;
};

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_RNG_H_
