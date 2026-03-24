/// @file boundary.cpp

#include "model/boundary.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace coverwise {
namespace model {

namespace {

/// @brief Format an integer value as a string.
std::string FormatInteger(int64_t value) { return std::to_string(value); }

/// @brief Format a float value as a string, trimming trailing zeros.
std::string FormatFloat(double value) {
  std::ostringstream oss;
  oss << value;
  return oss.str();
}

/// @brief Try to parse a string as a double. Returns false if not numeric.
bool TryParseDouble(const std::string& s, double& out) {
  if (s.empty()) return false;
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end == s.c_str() + s.size();
}

}  // namespace

Parameter ExpandBoundaryValues(const Parameter& param,
                               const BoundaryConfig& config) {
  // Generate boundary values.
  std::vector<double> boundary_nums;
  if (config.type == BoundaryConfig::Type::kInteger) {
    double step = 1.0;
    boundary_nums = {
        config.min_value - step,  config.min_value,     config.min_value + step,
        config.max_value - step,  config.max_value,     config.max_value + step,
    };
  } else {
    boundary_nums = {
        config.min_value - config.step, config.min_value,
        config.min_value + config.step, config.max_value - config.step,
        config.max_value,               config.max_value + config.step,
    };
  }

  // Collect existing values as doubles (for dedup), and track non-numeric values.
  std::set<double> seen_nums;
  std::vector<std::string> non_numeric_values;
  for (const auto& v : param.values) {
    double d;
    if (TryParseDouble(v, d)) {
      seen_nums.insert(d);
    } else {
      non_numeric_values.push_back(v);
    }
  }

  // Add boundary values (dedup).
  for (double bv : boundary_nums) {
    seen_nums.insert(bv);
  }

  // Sort numerically and format.
  std::vector<double> sorted_nums(seen_nums.begin(), seen_nums.end());
  std::sort(sorted_nums.begin(), sorted_nums.end());

  std::vector<std::string> expanded_values;
  expanded_values.reserve(sorted_nums.size() + non_numeric_values.size());

  for (double d : sorted_nums) {
    if (config.type == BoundaryConfig::Type::kInteger) {
      expanded_values.push_back(FormatInteger(static_cast<int64_t>(std::round(d))));
    } else {
      expanded_values.push_back(FormatFloat(d));
    }
  }

  // Append non-numeric values at the end.
  for (auto& v : non_numeric_values) {
    expanded_values.push_back(v);
  }

  // Build the result parameter. Preserve name, drop invalid/aliases since
  // boundary expansion changes the value set.
  Parameter result(param.name, std::move(expanded_values));
  return result;
}

}  // namespace model
}  // namespace coverwise
