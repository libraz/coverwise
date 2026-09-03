/// @file options_validation.cpp

#include "model/options_validation.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

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

/// @brief Charge the caller's strings against the documented byte budgets.
///
/// std::string already holds UTF-8, so size() is the byte length the JavaScript
/// surfaces compute with TextEncoder. Both the per-string and the aggregate
/// bound are documented input limits, so they belong to the acceptance contract
/// rather than to any one surface's reader.
///
/// The model's own strings — every kind but kRowValue — are charged whatever
/// the caller says. Row values are charged here only when no reader counted
/// them: a row reaches the engine as value indices, and the text of a position
/// that did not resolve is kept so the diagnostics can quote it back. A surface
/// that read the row already counted that text and charging it again would cost
/// a caller twice for text they wrote once; a caller that read nothing has it
/// counted nowhere else, and leaving it would put the one path with no reader
/// outside the published budget entirely.
Error ValidateStringBudget(const GenerateOptions& options, ChargedText charged) {
  size_t aggregate = charged.bytes();
  // A caller's own total can exhaust the budget on its own, and a model always
  // has at least a parameter name to charge, but the verdict must not depend on
  // there being something left to walk.
  if (aggregate > kMaxAggregateStringBytes) {
    return Invalid(AggregateBudgetExceededMessage());
  }
  // The context is composed only for a string that is being refused: building
  // one per charged string would cost more than the check it describes.
  auto account = [&aggregate](const std::string& value, ChargedString kind,
                              const ChargedStringLocation& location) {
    if (value.size() > kMaxStringBytes) {
      return Invalid(StringBudgetExceededMessage(ChargedStringContext(kind, location)));
    }
    aggregate += value.size();
    if (aggregate > kMaxAggregateStringBytes) {
      return Invalid(AggregateBudgetExceededMessage());
    }
    return Error{};
  };

  for (const auto& param : options.parameters) {
    if (auto error = account(param.name, ChargedString::kParameterName, {param.name});
        !error.ok()) {
      return error;
    }
    for (size_t index = 0; index < param.values.size(); ++index) {
      if (auto error =
              account(param.values[index], ChargedString::kParameterValue, {param.name, index});
          !error.ok()) {
        return error;
      }
    }
    // Both metadata lists run parallel to the value list, so a position in them
    // is the value index a refusal names.
    const auto& all_aliases = param.all_aliases();
    for (size_t index = 0; index < all_aliases.size(); ++index) {
      for (const auto& alias : all_aliases[index]) {
        if (auto error = account(alias, ChargedString::kValueAlias, {param.name, index});
            !error.ok()) {
          return error;
        }
      }
    }
    const auto& equivalence_classes = param.equivalence_classes();
    for (size_t index = 0; index < equivalence_classes.size(); ++index) {
      if (auto error = account(equivalence_classes[index], ChargedString::kEquivalenceClass,
                               {param.name, index});
          !error.ok()) {
        return error;
      }
    }
  }
  for (const auto& expression : options.constraint_expressions) {
    if (auto error = account(expression, ChargedString::kConstraintExpression, {}); !error.ok()) {
      return error;
    }
  }
  for (const auto& sub_model : options.sub_models) {
    for (const auto& name : sub_model.parameter_names) {
      if (auto error = account(name, ChargedString::kSubModelParameterName, {}); !error.ok()) {
        return error;
      }
    }
  }
  for (const auto& [param_name, value_weights] : options.weights.entries) {
    if (auto error = account(param_name, ChargedString::kWeightParameterName, {}); !error.ok()) {
      return error;
    }
    for (const auto& [value_name, weight] : value_weights) {
      (void)weight;
      if (auto error = account(value_name, ChargedString::kWeightValueName, {}); !error.ok()) {
        return error;
      }
    }
  }
  if (!charged.rows_counted()) {
    for (size_t row = 0; row < options.seeds.size(); ++row) {
      for (const auto& text : options.seeds[row].unresolved) {
        if (auto error = account(text, ChargedString::kRowValue, {"seeds", row}); !error.ok()) {
          return error;
        }
      }
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

/// @brief The whole rule set, judged with @p charged already spent.
Error ValidateOptions(const GenerateOptions& options, ChargedText charged) {
  if (auto budget_error = ValidateStringBudget(options, charged); !budget_error.ok()) {
    return budget_error;
  }
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
    // Which key has claimed each value so far, so a second key naming the same
    // value is caught here rather than resolved by whichever key the surface's
    // map happened to hand over first.
    std::vector<const std::string*> claimed_by(param->values.size(), nullptr);
    for (const auto& [value_name, weight] : value_weights) {
      const uint32_t value_index = ResolveValueName(*param, value_name);
      if (value_index == UINT32_MAX) {
        return Invalid("Unknown value in weights: " + param_name + "=" + value_name);
      }
      if (!std::isfinite(weight) || weight <= 0.0) {
        return Invalid("Weight must be finite and positive: " + param_name + "=" + value_name);
      }
      // Two keys naming one value carry two weights for it, and only one can
      // apply. A key spelled the way the model declares the value settles that
      // outright, which is how a weight keyed by an alias keeps working beside
      // one keyed by the value itself. With no declared spelling among them the
      // winner would come down to the order the caller's map is walked in, and
      // that order is not the same on every surface — so the model is refused
      // instead of weighted differently depending on where it was run.
      const std::string* claimed = claimed_by[value_index];
      const std::string& declared = param->values[value_index];
      if (claimed != nullptr && *claimed != declared && value_name != declared) {
        return Invalid("Ambiguous value in weights: " + param_name + "=" + *claimed + " and " +
                       param_name + "=" + value_name + " name the same value");
      }
      if (claimed == nullptr || value_name == declared) {
        claimed_by[value_index] = &value_name;
      }
    }
  }
  return {};
}

}  // namespace

std::string ChargedStringContext(ChargedString kind, const ChargedStringLocation& location) {
  const std::string subject(location.subject);
  switch (kind) {
    case ChargedString::kParameterName:
      return "Parameter name '" + subject + "'";
    case ChargedString::kParameterValue:
      return subject + "[" + std::to_string(location.index) + "]";
    case ChargedString::kValueAlias:
      return "Alias at " + subject + "[" + std::to_string(location.index) + "]";
    case ChargedString::kEquivalenceClass:
      return "Class at " + subject + "[" + std::to_string(location.index) + "]";
    case ChargedString::kConstraintExpression:
      return "Constraint expression";
    case ChargedString::kSubModelParameterName:
      return "Sub-model parameter name";
    case ChargedString::kWeightParameterName:
      return "Weight parameter name";
    case ChargedString::kWeightValueName:
      return "Weight value name";
    case ChargedString::kRowValue:
      return "Value in " + subject + " row " + std::to_string(location.index);
  }
  // Every kind is answered above, which -Wswitch holds to; this satisfies the
  // return-type check for a value outside the enumeration.
  return subject;
}

std::string StringBudgetExceededMessage(const std::string& context) {
  return context + " exceeds " + std::to_string(kMaxStringBytes) + " UTF-8 bytes";
}

std::string AggregateBudgetExceededMessage() {
  return "Input strings exceed " + std::to_string(kMaxAggregateStringBytes) + " UTF-8 bytes";
}

Error ValidateGenerateOptions(const GenerateOptions& options) {
  return ValidateOptions(options, ChargedText::None());
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

AcceptedOptions AcceptOptions(GenerateOptions options, ChargedText charged) {
  // Expansion runs first so every later rule is applied to the value space the
  // engine will use. Judging the declared values instead would, for instance,
  // reject a boundary parameter whose only spelled-out value is an invalid
  // sentinel, even though expansion is about to supply six valid ones.
  auto expansion_error = ExpandBoundaries(options);
  if (!expansion_error.ok()) return AcceptedOptions(std::move(expansion_error));
  auto validation_error = ValidateOptions(options, charged);
  if (!validation_error.ok()) return AcceptedOptions(std::move(validation_error));
  return AcceptedOptions(ValidatedOptions(std::move(options)));
}

}  // namespace model
}  // namespace coverwise
