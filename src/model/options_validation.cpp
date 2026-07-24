/// @file options_validation.cpp

#include "model/options_validation.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

#include "util/string_util.h"

namespace coverwise {
namespace model {
namespace {

Error Invalid(std::string message, std::string detail = {}) {
  return {Error::Code::kInvalidInput, std::move(message), std::move(detail)};
}

const Parameter* FindParameter(const std::vector<Parameter>& params, const std::string& name) {
  for (const auto& param : params) {
    if (param.name == name) return &param;
  }
  return nullptr;
}

}  // namespace

Error ValidateGenerateOptions(const GenerateOptions& options) {
  // A boundary-configured parameter may intentionally start empty because the
  // boundary range supplies its value set. Validate all other parameter
  // semantics before expansion by using a temporary placeholder value.
  auto validation_params = options.parameters;
  for (auto& param : validation_params) {
    if (param.values.empty() && options.boundary_configs.count(param.name) > 0) {
      if (!param.invalid().empty() || !param.all_aliases().empty() ||
          !param.equivalence_classes().empty()) {
        return Invalid("Metadata requires explicit values for boundary parameter " + param.name);
      }
      param.values.push_back("__coverwise_boundary_placeholder__");
    }
  }
  auto param_error = ValidateParameters(validation_params);
  if (!param_error.ok()) return param_error;
  if (options.parameters.empty()) return Invalid("At least one parameter is required");
  if (options.strength == 0 || options.strength > options.parameters.size()) {
    return Invalid("Strength must be between 1 and parameter count",
                   "strength=" + std::to_string(options.strength) +
                       ", parameters=" + std::to_string(options.parameters.size()));
  }
  if (options.seed > std::numeric_limits<uint32_t>::max()) {
    return Invalid("Seed must be an integer in [0, 4294967295]");
  }

  for (const auto& sub_model : options.sub_models) {
    if (sub_model.parameter_names.empty()) {
      return Invalid("Sub-model must contain at least one parameter");
    }
    std::unordered_set<std::string> seen;
    for (const auto& name : sub_model.parameter_names) {
      if (!seen.insert(name).second) {
        return Invalid("Duplicate parameter in sub-model: " + name);
      }
      if (FindParameter(options.parameters, name) == nullptr) {
        return Invalid("Unknown parameter in sub-model: " + name);
      }
    }
    if (sub_model.strength == 0 || sub_model.strength > sub_model.parameter_names.size()) {
      return Invalid("Sub-model strength must be between 1 and its parameter count");
    }
  }

  for (const auto& [param_name, value_weights] : options.weights.entries) {
    const auto* param = FindParameter(options.parameters, param_name);
    if (param == nullptr) return Invalid("Unknown parameter in weights: " + param_name);
    for (const auto& [value_name, weight] : value_weights) {
      if (param->find_value_index(value_name) == UINT32_MAX) {
        return Invalid("Unknown value in weights: " + param_name + "=" + value_name);
      }
      if (!std::isfinite(weight) || weight <= 0.0) {
        return Invalid("Weight must be finite and positive: " + param_name + "=" + value_name);
      }
    }
  }

  for (const auto& [param_name, config] : options.boundary_configs) {
    const auto* param = FindParameter(options.parameters, param_name);
    if (param == nullptr) return Invalid("Unknown parameter in boundary config: " + param_name);
    if (!std::isfinite(config.min_value) || !std::isfinite(config.max_value) ||
        config.min_value > config.max_value) {
      return Invalid("Boundary range must be finite and ordered for parameter " + param_name);
    }
    const double boundary_values[] = {
        config.type == BoundaryConfig::Type::kInteger ? config.min_value - 1.0
                                                      : config.min_value - config.step,
        config.min_value,
        config.type == BoundaryConfig::Type::kInteger ? config.min_value + 1.0
                                                      : config.min_value + config.step,
        config.type == BoundaryConfig::Type::kInteger ? config.max_value - 1.0
                                                      : config.max_value - config.step,
        config.max_value,
        config.type == BoundaryConfig::Type::kInteger ? config.max_value + 1.0
                                                      : config.max_value + config.step,
    };
    for (double value : boundary_values) {
      if (!std::isfinite(value)) {
        return Invalid("Boundary expansion must produce finite values for parameter " + param_name);
      }
    }
    std::unordered_set<double> numeric_identities;
    for (const auto& value : param->values) {
      if (!util::IsNumeric(value)) continue;
      double numeric = util::ToDouble(value);
      if (!std::isfinite(numeric)) {
        return Invalid("Boundary parameter contains a non-finite numeric value: " + param_name +
                       "=" + value);
      }
      if (!numeric_identities.insert(numeric).second) {
        return Invalid("Boundary parameter contains duplicate numeric identities: " + param_name);
      }
    }
    if (config.type == BoundaryConfig::Type::kFloat) {
      if (!std::isfinite(config.step) || config.step <= 0.0) {
        return Invalid("Boundary step must be finite and positive for parameter " + param_name);
      }
    } else {
      // Integer endpoints and values must be JS safe integers (|v| <= 2^53-1),
      // matching Number.isSafeInteger on the TypeScript surfaces so native/WASM
      // and pure-TS accept or reject the same models.
      constexpr double kMaxSafeInteger = 9007199254740991.0;  // 2^53 - 1
      auto is_safe_integer = [](double v) {
        return std::isfinite(v) && std::floor(v) == v && std::abs(v) <= kMaxSafeInteger;
      };
      if (!is_safe_integer(config.min_value) || !is_safe_integer(config.max_value)) {
        return Invalid("Integer boundary endpoints must be safe integers for " + param_name);
      }
      for (const auto& value : param->values) {
        if (!util::IsNumeric(value)) continue;
        double numeric = util::ToDouble(value);
        if (!is_safe_integer(numeric)) {
          return Invalid(
              "Integer boundary parameter contains a non-integral or out-of-range value: " +
              param_name + "=" + value);
        }
      }
    }
  }
  return {};
}

}  // namespace model
}  // namespace coverwise
