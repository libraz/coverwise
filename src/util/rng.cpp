/// @file rng.cpp

#include "util/rng.h"

namespace coverwise {
namespace util {

uint32_t Rng::NextUint32(uint32_t max) {
  if (max == 0) return 0;
  std::uniform_int_distribution<uint32_t> dist(0, max - 1);
  return dist(engine_);
}

}  // namespace util
}  // namespace coverwise
