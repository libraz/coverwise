#include <coverwise.h>

// The consumer sets a lower standard on purpose; linking coverwise has to raise
// the translation unit to C++17.
#if defined(_MSVC_LANG)
static_assert(_MSVC_LANG >= 201703L, "coverwise did not raise the consumer to C++17");
#else
static_assert(__cplusplus >= 201703L, "coverwise did not raise the consumer to C++17");
#endif

int main() {
  coverwise::model::GenerateOptions options;
  options.parameters = {{"p", {"a", "b"}}};
  options.strength = 1;
  const auto result = coverwise::core::Generate(options);
  return result.error.ok() && result.tests.size() == 2 ? 0 : 1;
}
