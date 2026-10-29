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

/// @brief Number of decimal places in the shortest round-trip rendering of @p value.
int DecimalPlaces(double value) {
  const std::string text = JsNumberToString(std::fabs(value));
  const size_t exponent_at = text.find('e');
  const std::string mantissa = text.substr(0, exponent_at);
  const size_t dot = mantissa.find('.');
  int places = dot == std::string::npos ? 0 : static_cast<int>(mantissa.size() - dot - 1);
  if (exponent_at != std::string::npos) places -= std::stoi(text.substr(exponent_at + 1));
  return std::max(places, 0);
}

/// @brief Round @p value to @p places decimals, so accumulated binary error does not leak.
///
/// Rounds half up, like JS Math.round, so the TypeScript port snaps identically.
double SnapToDecimals(double value, int places) {
  if (places > 15) return value;
  double scale = 1.0;
  for (int i = 0; i < places; ++i) scale *= 10.0;
  const double scaled = value * scale;
  if (!(std::fabs(scaled) < 9007199254740992.0)) return value;
  return std::floor(scaled + 0.5) / scale;
}

}  // namespace

Parameter ExpandBoundaryValues(const Parameter& param, const BoundaryConfig& config) {
  return ExpandBoundaryValues(param, config, nullptr);
}

Parameter ExpandBoundaryValues(const Parameter& param, const BoundaryConfig& config, Error* error) {
  auto reject_metadata = [&](const std::string& field) {
    Error metadata_error{Error::Code::kInvalidInput,
                         "Invalid metadata length for parameter '" + param.name + "': " + field,
                         ""};
    if (error != nullptr) *error = metadata_error;
    return param;
  };
  if (!param.invalid().empty() && param.invalid().size() != param.values.size()) {
    return reject_metadata("invalid");
  }
  if (!param.all_aliases().empty() && param.all_aliases().size() != param.values.size()) {
    return reject_metadata("aliases");
  }
  if (!param.equivalence_classes().empty() &&
      param.equivalence_classes().size() != param.values.size()) {
    return reject_metadata("equivalence classes");
  }
  if (error != nullptr) *error = {};

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
    // Snap to the precision the caller wrote, so 0.3 - 0.1 is 0.2 rather than 0.19999999999999998.
    const int places = std::max({DecimalPlaces(config.min_value), DecimalPlaces(config.max_value),
                                 DecimalPlaces(config.step)});
    for (double& bv : boundary_nums) {
      if (std::isfinite(bv)) bv = SnapToDecimals(bv, places);
    }
  }

  // Map numeric identity to the original value index. Keeping the original
  // spelling (e.g. "1.0") avoids changing seed/constraint identity when the
  // numeric value is deduplicated with a generated boundary.
  std::map<double, std::optional<size_t>> numeric_values;
  std::vector<size_t> non_numeric_indices;
  for (size_t i = 0; i < param.values.size(); ++i) {
    const auto& v = param.values[i];
    const bool is_numeric = IsNumeric(v);
    const double numeric = is_numeric ? ToDouble(v) : 0.0;
    if (is_numeric && std::isfinite(numeric)) {
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
  auto out_of_range = [&](double number) {
    return number < config.min_value || number > config.max_value;
  };
  const bool generated_out_of_range =
      std::any_of(numeric_values.begin(), numeric_values.end(),
                  [&](const auto& entry) { return !entry.second && out_of_range(entry.first); });
  const bool preserve_invalid = !param.invalid().empty() || generated_out_of_range;
  const bool preserve_aliases = !param.all_aliases().empty();
  const bool preserve_classes = !param.equivalence_classes().empty();

  auto append_metadata = [&](std::optional<size_t> original, double number) {
    if (preserve_invalid) {
      expanded_invalid.push_back(original ? !param.invalid().empty() && param.invalid()[*original]
                                          : out_of_range(number));
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
    append_metadata(original, number);
  }

  // Append non-numeric values at the end.
  for (size_t original : non_numeric_indices) {
    expanded_values.push_back(param.values[original]);
    append_metadata(original, 0.0);
  }

  // Preserve per-value metadata by the original value's numeric/string identity.
  // Newly generated boundary values receive the neutral defaults, except that
  // those outside the range are invalid.
  Parameter result(param.name, std::move(expanded_values));
  if (preserve_invalid) result.set_invalid(std::move(expanded_invalid));
  if (preserve_aliases) result.set_aliases(std::move(expanded_aliases));
  if (preserve_classes) result.set_equivalence_classes(std::move(expanded_classes));
  return result;
}

}  // namespace model
}  // namespace coverwise
