/// @file boundary.h
/// @brief Boundary value expansion for numeric parameters.

#ifndef COVERWISE_MODEL_BOUNDARY_H_
#define COVERWISE_MODEL_BOUNDARY_H_

#include <string>
#include <vector>

#include "model/parameter.h"

namespace coverwise {
namespace model {

/// @brief Configuration for boundary value expansion of a numeric parameter.
struct BoundaryConfig {
  enum class Type { kInteger, kFloat };
  Type type = Type::kInteger;
  double min_value = 0;
  double max_value = 0;
  double step = 1.0;  ///< Step size for float type (default 1.0 for integer).
};

/// @brief Expand a parameter's values to include boundary values.
///
/// For integer type, adds: min-1, min, min+1, max-1, max, max+1.
/// For float type, adds: min-step, min, min+step, max-step, max, max+step.
/// Merges with existing values (dedup) and sorts numerically.
///
/// @param param The original parameter.
/// @param config The boundary configuration specifying the range.
/// @return A new Parameter with expanded values.
Parameter ExpandBoundaryValues(const Parameter& param, const BoundaryConfig& config);

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_BOUNDARY_H_
