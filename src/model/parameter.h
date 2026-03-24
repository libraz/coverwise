/// @file parameter.h
/// @brief Parameter definition for combinatorial test generation.

#ifndef COVERWISE_MODEL_PARAMETER_H_
#define COVERWISE_MODEL_PARAMETER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace coverwise {
namespace model {

/// @brief A test parameter with a name and a set of discrete values.
struct Parameter {
  std::string name;
  std::vector<std::string> values;

  /// @brief Returns the number of values for this parameter.
  uint32_t size() const { return static_cast<uint32_t>(values.size()); }
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_PARAMETER_H_
