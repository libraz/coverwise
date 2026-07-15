/// @file boundary.cpp

#include "model/boundary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "util/string_util.h"

namespace coverwise {
namespace model {

using util::IsNumeric;
using util::JsNumberToString;
using util::ToDouble;

namespace {

/// @brief Format an integer value as a string (matches JS String(value)).
std::string FormatInteger(double value) { return JsNumberToString(std::round(value)); }

/// @brief Format a float value as the shortest round-trip string.
///
/// Uses the canonical JS Number-to-String algorithm so newly computed boundary
/// values (min-step, max+step, ...) render identically to the TypeScript port.
std::string FormatFloat(double value) { return JsNumberToString(value); }

}  // namespace

Parameter ExpandBoundaryValues(const Parameter& param, const BoundaryConfig& config) {
  // Generate boundary values.
  std::vector<double> boundary_nums;
  if (config.type == BoundaryConfig::Type::kInteger) {
    double step = 1.0;
    boundary_nums = {
        config.min_value - step, config.min_value, config.min_value + step,
        config.max_value - step, config.max_value, config.max_value + step,
    };
  } else {
    boundary_nums = {
        config.min_value - config.step, config.min_value, config.min_value + config.step,
        config.max_value - config.step, config.max_value, config.max_value + config.step,
    };
  }

  // Map numeric identity to the original value index. Keeping the original
  // spelling (e.g. "1.0") avoids changing seed/constraint identity when the
  // numeric value is deduplicated with a generated boundary.
  std::map<double, std::optional<size_t>> numeric_values;
  std::vector<size_t> non_numeric_indices;
  for (size_t i = 0; i < param.values.size(); ++i) {
    const auto& v = param.values[i];
    const double numeric = IsNumeric(v) ? ToDouble(v) : 0.0;
    if (IsNumeric(v) && std::isfinite(numeric)) {
      numeric_values.emplace(numeric, i);
    } else {
      non_numeric_indices.push_back(i);
    }
  }

  // Add boundary values (dedup).
  for (double bv : boundary_nums) {
    if (std::isfinite(bv)) numeric_values.emplace(bv, std::nullopt);
  }

  std::vector<std::string> expanded_values;
  expanded_values.reserve(numeric_values.size() + non_numeric_indices.size());
  std::vector<bool> expanded_invalid;
  std::vector<std::vector<std::string>> expanded_aliases;
  std::vector<std::string> expanded_classes;
  const bool preserve_invalid = !param.invalid().empty();
  const bool preserve_aliases = !param.all_aliases().empty();
  const bool preserve_classes = !param.equivalence_classes().empty();

  auto append_metadata = [&](std::optional<size_t> original) {
    if (preserve_invalid) {
      expanded_invalid.push_back(original ? param.invalid()[*original] : false);
    }
    if (preserve_aliases) {
      expanded_aliases.push_back(original ? param.all_aliases()[*original]
                                          : std::vector<std::string>{});
    }
    if (preserve_classes) {
      expanded_classes.push_back(original ? param.equivalence_classes()[*original] : "");
    }
  };

  for (const auto& [number, original] : numeric_values) {
    if (original) {
      expanded_values.push_back(param.values[*original]);
    } else if (config.type == BoundaryConfig::Type::kInteger) {
      expanded_values.push_back(FormatInteger(number));
    } else {
      expanded_values.push_back(FormatFloat(number));
    }
    append_metadata(original);
  }

  // Append non-numeric values at the end.
  for (size_t original : non_numeric_indices) {
    expanded_values.push_back(param.values[original]);
    append_metadata(original);
  }

  // Preserve per-value metadata by the original value's numeric/string identity.
  // Newly generated boundary values receive the neutral metadata defaults.
  Parameter result(param.name, std::move(expanded_values));
  if (preserve_invalid) result.set_invalid(std::move(expanded_invalid));
  if (preserve_aliases) result.set_aliases(std::move(expanded_aliases));
  if (preserve_classes) result.set_equivalence_classes(std::move(expanded_classes));
  return result;
}

}  // namespace model
}  // namespace coverwise
