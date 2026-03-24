/// @file bindings.cpp
/// @brief Emscripten embind bindings for coverwise WASM module.

#ifdef __EMSCRIPTEN__

#include <emscripten/bind.h>

#include "core/generator.h"

using namespace emscripten;

// TODO: Implement WASM bindings
// - Expose generate() with flat parameter arrays
// - Return results as typed arrays
// - Keep JSON parsing on JS side

EMSCRIPTEN_BINDINGS(coverwise) {
  // Placeholder — bindings will be added as core API stabilizes
}

#endif  // __EMSCRIPTEN__
