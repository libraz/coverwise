/// @file options_validation.cpp

#include "model/options_validation.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>

#include "model/boundary.h"
#include "model/limits.h"
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

/// @brief Charge every string in the model against the documented byte budgets.
///
/// std::string already holds UTF-8, so size() is the byte length the JavaScript
/// surfaces compute with TextEncoder. Both the per-string and the aggregate
/// bound are documented input limits, so they belong to the acceptance contract
/// rather than to any one surface's reader.
Error ValidateStringBudget(const GenerateOptions& options) {
  size_t aggregate = 0;
  auto account = [&aggregate](const std::string& value, const std::string& context) {
    if (value.size() > kMaxStringBytes) {
      return Invalid(context + " exceeds " + std::to_string(kMaxStringBytes) + " UTF-8 bytes");
    }
    aggregate += value.size();
    if (aggregate > kMaxAggregateStringBytes) {
      return Invalid("Input strings exceed " + std::to_string(kMaxAggregateStringBytes) +
                     " UTF-8 bytes");
    }
    return Error{};
  };

  for (const auto& param : options.parameters) {
    if (auto error = account(param.name, "Parameter name '" + param.name + "'"); !error.ok()) {
      return error;
    }
    for (size_t index = 0; index < param.values.size(); ++index) {
      const std::string context = param.name + "[" + std::to_string(index) + "]";
      if (auto error = account(param.values[index], context); !error.ok()) return error;
    }
    for (const auto& value_aliases : param.all_aliases()) {
      for (const auto& alias : value_aliases) {
        if (auto error = account(alias, "Alias in parameter '" + param.name + "'"); !error.ok()) {
          return error;
        }
      }
    }
    for (const auto& equivalence_class : param.equivalence_classes()) {
      if (auto error = account(equivalence_class, "Class in parameter '" + param.name + "'");
          !error.ok()) {
        return error;
      }
    }
  }
  for (const auto& expression : options.constraint_expressions) {
    if (auto error = account(expression, "Constraint expression"); !error.ok()) return error;
  }
  for (const auto& sub_model : options.sub_models) {
    for (const auto& name : sub_model.parameter_names) {
      if (auto error = account(name, "Sub-model parameter name"); !error.ok()) return error;
    }
  }
  for (const auto& [param_name, value_weights] : options.weights.entries) {
    if (auto error = account(param_name, "Weight parameter name"); !error.ok()) return error;
    for (const auto& [value_name, weight] : value_weights) {
      (void)weight;
      if (auto error = account(value_name, "Weight value name"); !error.ok()) return error;
    }
  }
  return {};
}

/// @brief Validate every boundary config against the declared value list.
///
/// Runs before expansion because the checks are about the configured range and
/// the values the caller wrote down — after expansion the generated boundary
/// values would mask, for instance, a duplicate numeric identity.
Error ValidateBoundaryConfigs(const GenerateOptions& options) {
  for (const auto& [param_name, config] : options.boundary_configs) {
    const auto* param = FindParameter(options.parameters, param_name);
    if (param == nullptr) return Invalid("Unknown parameter in boundary config: " + param_name);
    if (param->values.empty() && (!param->invalid().empty() || !param->all_aliases().empty() ||
                                  !param->equivalence_classes().empty())) {
      return Invalid("Metadata requires explicit values for boundary parameter " + param_name);
    }
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
      // Integer expansion always steps by one, so a caller asking for anything
      // else is asking for a value set the engine will not produce. Rejecting is
      // the only answer that keeps the model JSON meaning one thing everywhere.
      if (config.step != 1.0) {
        return Invalid("Integer boundary step must be 1 for parameter " + param_name);
      }
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

}  // namespace

Error ValidateGenerateOptions(const GenerateOptions& options) {
  if (auto budget_error = ValidateStringBudget(options); !budget_error.ok()) return budget_error;
  auto boundary_error = ValidateBoundaryConfigs(options);
  if (!boundary_error.ok()) return boundary_error;

  // A boundary-configured parameter may intentionally start empty because the
  // boundary range supplies its value set. Validate all other parameter
  // semantics before expansion by using a temporary placeholder value.
  auto validation_params = options.parameters;
  for (auto& param : validation_params) {
    if (param.values.empty() && options.boundary_configs.count(param.name) > 0) {
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
  if (options.constraint_expressions.size() > kMaxConstraints) {
    return Invalid("Constraint count " + std::to_string(options.constraint_expressions.size()) +
                   " exceeds maximum of " + std::to_string(kMaxConstraints));
  }
  if (options.seeds.size() > kMaxTests) {
    return Invalid("Seed test count " + std::to_string(options.seeds.size()) +
                   " exceeds maximum of " + std::to_string(kMaxTests));
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
  return {};
}

Error ExpandBoundaries(GenerateOptions& options) {
  if (options.boundary_configs.empty()) return {};
  auto config_error = ValidateBoundaryConfigs(options);
  if (!config_error.ok()) return config_error;
  for (auto& param : options.parameters) {
    auto it = options.boundary_configs.find(param.name);
    if (it == options.boundary_configs.end()) continue;
    Error expansion_error;
    param = ExpandBoundaryValues(param, it->second, &expansion_error);
    if (!expansion_error.ok()) return expansion_error;
  }
  options.boundary_configs.clear();
  return {};
}

AcceptedOptions AcceptOptions(GenerateOptions options) {
  // Expansion runs first so every later rule is applied to the value space the
  // engine will use. Judging the declared values instead would, for instance,
  // reject a boundary parameter whose only spelled-out value is an invalid
  // sentinel, even though expansion is about to supply six valid ones.
  auto expansion_error = ExpandBoundaries(options);
  if (!expansion_error.ok()) return AcceptedOptions(std::move(expansion_error));
  auto validation_error = ValidateGenerateOptions(options);
  if (!validation_error.ok()) return AcceptedOptions(std::move(validation_error));
  return AcceptedOptions(ValidatedOptions(std::move(options)));
}

}  // namespace model
}  // namespace coverwise
