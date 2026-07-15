#include <coverwise.h>

int main() {
  coverwise::model::GenerateOptions options;
  options.parameters = {{"p", {"a", "b"}}};
  options.strength = 1;
  const auto result = coverwise::core::Generate(options);
  return result.error.ok() && result.coverage == 1.0 ? 0 : 1;
}
