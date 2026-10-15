#include "model_reader.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "model/boundary.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/limits.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "util/string_util.h"

namespace coverwise {
namespace cli {

// The documented input limits live in model/limits.h and are the same on every
// surface; the CLI reads them from there instead of restating the numbers.
using coverwise::model::kMaxConstraints;
using coverwise::model::kMaxParameters;
using coverwise::model::kMaxStringBytes;
using coverwise::model::kMaxTests;
using coverwise::model::kMaxValuesPerParameter;

/// @brief The failure a reader reports: an argument, a file, or a JSON document
/// the CLI could not turn into a model is invalid input by definition.
///
/// Readers hand this back rather than a bare string so a message never travels
/// separately from the code and detail it belongs to — the composition into user
/// text happens once, in SurfaceError, at the surface.
coverwise::model::Error ReaderError(std::string message) {
  return {coverwise::model::Error::Code::kInvalidInput, std::move(message), ""};
}

/// @brief Parse an optional JSON object field as an exact uint32.
bool ParseOptionalUint32(const JsonValue& object, const std::string& field, uint32_t minimum,
                         uint32_t& output, std::string& error) {
  if (!object.HasKey(field)) return true;
  const auto& value = object[field];
  if (value.type != JsonType::kNumber || value.number_val != std::floor(value.number_val) ||
      value.number_val < static_cast<double>(minimum) || value.number_val > 4294967295.0) {
    error = field + " must be an integer in [" + std::to_string(minimum) + ", 4294967295]";
    return false;
  }
  output = static_cast<uint32_t>(value.number_val);
  return true;
}

namespace {

// ---------------------------------------------------------------------------
// Parse coverwise model objects from JSON.
// ---------------------------------------------------------------------------

/// @brief Parse parameters from a JSON array of {name, values} objects.
bool ParseParameters(const JsonValue& json, std::vector<coverwise::model::Parameter>& params,
                     std::string& error) {
  if (json.type != JsonType::kArray) {
    error = "parameters must be a JSON array";
    return false;
  }
  if (json.array_val.size() > kMaxParameters) {
    error = "parameters exceed maximum of " + std::to_string(kMaxParameters);
    return false;
  }
  for (size_t i = 0; i < json.array_val.size(); ++i) {
    const auto& p = json.array_val[i];
    if (p.type != JsonType::kObject) {
      error = "parameter " + std::to_string(i) + " must be an object";
      return false;
    }
    coverwise::model::Parameter param;
    const auto& name_val = p["name"];
    if (name_val.type != JsonType::kString) {
      error = "parameter " + std::to_string(i) + " missing 'name' string";
      return false;
    }
    param.name = name_val.string_val;

    const auto& values_val = p["values"];
    if (values_val.type != JsonType::kArray) {
      error = "parameter '" + param.name + "' missing 'values' array";
      return false;
    }
    if (values_val.array_val.size() > kMaxValuesPerParameter) {
      error = "parameter '" + param.name + "' values exceed maximum of " +
              std::to_string(kMaxValuesPerParameter);
      return false;
    }
    bool has_any_invalid = false;
    bool has_any_aliases = false;
    bool has_any_classes = false;
    std::vector<bool> invalid_flags;
    std::vector<std::vector<std::string>> aliases_list;
    std::vector<std::string> eq_classes;
    for (size_t j = 0; j < values_val.array_val.size(); ++j) {
      const auto& v = values_val.array_val[j];
      if (v.type == JsonType::kString) {
        param.values.push_back(v.string_val);
        invalid_flags.push_back(false);
        aliases_list.push_back({});
        eq_classes.push_back({});
      } else if (v.type == JsonType::kNumber) {
        // Convert number to its canonical JS string (matches every surface).
        param.values.push_back(coverwise::util::JsNumberToString(v.number_val));
        invalid_flags.push_back(false);
        aliases_list.push_back({});
        eq_classes.push_back({});
      } else if (v.type == JsonType::kBool) {
        param.values.push_back(v.bool_val ? "true" : "false");
        invalid_flags.push_back(false);
        aliases_list.push_back({});
        eq_classes.push_back({});
      } else if (v.type == JsonType::kObject) {
        // Object form: {"value": "...", "invalid": true, "aliases": ["..."]}
        const auto& val_field = v["value"];
        std::string val_str;
        if (val_field.type == JsonType::kString) {
          val_str = val_field.string_val;
        } else if (val_field.type == JsonType::kNumber) {
          val_str = coverwise::util::JsNumberToString(val_field.number_val);
        } else if (val_field.type == JsonType::kBool) {
          val_str = val_field.bool_val ? "true" : "false";
        } else {
          error = "parameter '" + param.name + "' value " + std::to_string(j) +
                  " object missing 'value' field";
          return false;
        }
        param.values.push_back(val_str);
        const auto& inv_field = v["invalid"];
        if (v.HasKey("invalid") && inv_field.type != JsonType::kBool) {
          error = "parameter '" + param.name + "' value " + std::to_string(j) +
                  " invalid flag must be a boolean";
          return false;
        }
        bool is_invalid = (inv_field.type == JsonType::kBool && inv_field.bool_val);
        invalid_flags.push_back(is_invalid);
        if (is_invalid) has_any_invalid = true;

        // Parse aliases.
        std::vector<std::string> val_aliases;
        const auto& aliases_field = v["aliases"];
        if (v.HasKey("aliases")) {
          if (aliases_field.type != JsonType::kArray) {
            error = "parameter '" + param.name + "' value " + std::to_string(j) +
                    " aliases must be an array";
            return false;
          }
          for (size_t k = 0; k < aliases_field.array_val.size(); ++k) {
            const auto& a = aliases_field.array_val[k];
            if (a.type == JsonType::kString && !a.string_val.empty()) {
              val_aliases.push_back(a.string_val);
            } else {
              error = "parameter '" + param.name + "' value " + std::to_string(j) + " alias " +
                      std::to_string(k) + " must be a non-empty string";
              return false;
            }
          }
          if (!val_aliases.empty()) has_any_aliases = true;
        }
        aliases_list.push_back(std::move(val_aliases));

        // Parse equivalence class.
        const auto& class_field = v["class"];
        if (v.HasKey("class") && class_field.type != JsonType::kString) {
          error = "parameter '" + param.name + "' value " + std::to_string(j) +
                  " class must be a string";
          return false;
        }
        if (class_field.type == JsonType::kString && !class_field.string_val.empty()) {
          eq_classes.push_back(class_field.string_val);
          has_any_classes = true;
        } else {
          eq_classes.push_back({});
        }
      } else {
        error = "parameter '" + param.name + "' value " + std::to_string(j) +
                " must be a string, number, boolean, or {value, invalid, aliases} object";
        return false;
      }
    }
    // Set invalid flags only if any value is actually invalid.
    if (has_any_invalid) {
      param.set_invalid(std::move(invalid_flags));
    }
    // Set aliases only if any value has aliases.
    if (has_any_aliases) {
      param.set_aliases(std::move(aliases_list));
    }
    // Set equivalence classes only if any value has a class.
    if (has_any_classes) {
      param.set_equivalence_classes(std::move(eq_classes));
    }
    const bool has_boundary = p.HasKey("type") || p.HasKey("range") || p.HasKey("step");
    if (param.values.empty() && !has_boundary) {
      error = "parameter '" + param.name + "' has no values";
      return false;
    }
    params.push_back(std::move(param));
  }
  if (params.empty()) {
    error = "parameters array is empty";
    return false;
  }
  return true;
}

/// @brief Parse boundary value configs from parameter JSON.
///
/// For each parameter with "type" ("integer" or "float") and "range" ([min, max]),
/// creates a BoundaryConfig. Optional "step" for float type.
bool ParseBoundaryConfigs(const JsonValue& json,
                          std::map<std::string, coverwise::model::BoundaryConfig>& configs,
                          std::string& error) {
  if (json.type != JsonType::kArray) {
    error = "parameters must be a JSON array";
    return false;
  }
  for (size_t i = 0; i < json.array_val.size(); ++i) {
    const auto& p = json.array_val[i];
    if (p.type != JsonType::kObject) continue;

    const auto& name_val = p["name"];
    if (name_val.type != JsonType::kString) continue;

    const auto& type_val = p["type"];
    const auto& range_val = p["range"];
    const bool has_boundary = p.HasKey("type") || p.HasKey("range") || p.HasKey("step");
    if (!has_boundary) continue;
    if (type_val.type != JsonType::kString || range_val.type != JsonType::kArray ||
        range_val.array_val.size() != 2) {
      error =
          "parameter '" + name_val.string_val + "' boundary requires type and a two-number range";
      return false;
    }
    if (range_val.array_val[0].type != JsonType::kNumber ||
        range_val.array_val[1].type != JsonType::kNumber) {
      error = "parameter '" + name_val.string_val + "' boundary range must contain numbers";
      return false;
    }

    coverwise::model::BoundaryConfig config;
    config.min_value = range_val.array_val[0].number_val;
    config.max_value = range_val.array_val[1].number_val;

    if (type_val.string_val == "integer") {
      config.type = coverwise::model::BoundaryConfig::Type::kInteger;
    } else if (type_val.string_val == "float") {
      config.type = coverwise::model::BoundaryConfig::Type::kFloat;
    } else {
      error = "parameter '" + name_val.string_val + "' has unknown boundary type";
      return false;
    }
    // `step` is carried through for both types, including integer, where the
    // acceptance rules reject anything other than 1. Defaulting it here instead
    // would drop the caller's request and silently generate a value set they
    // did not ask for.
    const auto& step_val = p["step"];
    if (p.HasKey("step") && step_val.type != JsonType::kNumber) {
      error = "parameter '" + name_val.string_val + "' boundary step must be a number";
      return false;
    }
    config.step = step_val.type == JsonType::kNumber ? step_val.number_val : 1.0;

    configs[name_val.string_val] = config;
  }
  return true;
}

}  // namespace

/// @brief Parse the parameter array into @p options and expand its value space.
///
/// Boundary expansion must happen before anything resolves a value name to an
/// index: test cases carry indices, so rows parsed against the declared values
/// would point into the wrong list once expansion sorts and inserts values.
/// Expanding here also means the acceptance rules judge the value space
/// generation actually uses — a boundary parameter whose only spelled-out value
/// is an invalid sentinel is well-formed, because expansion supplies the valid
/// ones.
/// @return an ok Error on success, otherwise the failure as the model layer
///         reported it — a model-layer Error is passed on whole rather than
///         flattened into a string, so its code and its detail still reach the
///         surface that renders it.
coverwise::model::Error ParseModelParameters(const JsonValue& parameters_json,
                                             coverwise::model::GenerateOptions& options) {
  std::string error;
  if (!ParseParameters(parameters_json, options.parameters, error)) {
    return ReaderError(std::move(error));
  }
  if (!ParseBoundaryConfigs(parameters_json, options.boundary_configs, error)) {
    return ReaderError(std::move(error));
  }
  if (auto expansion = coverwise::model::ExpandBoundaries(options); !expansion.ok()) {
    return expansion;
  }
  return coverwise::model::ValidateParameters(options.parameters);
}

/// @brief Parse test cases from a JSON array of objects with scalar values.
/// Each test object maps parameter names to values; the result carries value
/// indices matching the parameter definitions, or model::kUnassigned where a
/// kRecorded row does not match the model.
///
/// Row text is the largest dimension of an input, so it is charged against the
/// documented aggregate budget here, where it is read and before any of it
/// reaches the engine. This is the only place a row value is charged: the gate
/// walks the model's own strings and no row array reaches it as text.
bool ParseTests(const JsonValue& json, const std::vector<coverwise::model::Parameter>& params,
                TestRowPolicy policy, const char* field,
                std::vector<coverwise::model::TestCase>& tests,
                coverwise::model::ChargedTextReader& budget, std::string& error) {
  if (json.type != JsonType::kArray) {
    error = std::string(field) + " must be a JSON array";
    return false;
  }
  if (json.array_val.size() > kMaxTests) {
    error = std::string(field) + " exceed maximum of " + std::to_string(kMaxTests);
    return false;
  }

  std::unordered_map<std::string_view, size_t> parameter_indices;
  parameter_indices.reserve(params.size());
  for (size_t pi = 0; pi < params.size(); ++pi) {
    parameter_indices.emplace(params[pi].name, pi);
  }
  std::vector<const JsonValue*> members(params.size());

  for (size_t i = 0; i < json.array_val.size(); ++i) {
    const auto& t = json.array_val[i];
    if (t.type != JsonType::kObject) {
      error = std::string(field) + " " + std::to_string(i) + " must be an object";
      return false;
    }
    // A row's values are the caller's own strings; its keys are parameter names,
    // which the model already charges once each. Charging keys per row too would
    // make the budget shrink with the parameter count rather than bound the text
    // the caller actually supplied.
    //
    // Every string member is charged, whether or not its key names a declared
    // parameter: what the limit bounds is the text the caller handed over, not
    // the part of it the model happens to have somewhere to put.
    //
    // The per-string limit is applied here for the same reason, and the sentence
    // reporting it comes from the model layer. The aggregate total goes to the
    // acceptance gate, so neither limit's verdict is composed twice.
    for (const auto& member : t.object_vals) {
      if (member.type != JsonType::kString) continue;
      if (member.string_val.size() > kMaxStringBytes) {
        error =
            coverwise::model::StringBudgetExceededMessage(coverwise::model::ChargedStringContext(
                coverwise::model::ChargedString::kRowValue, {field, i}));
        return false;
      }
      budget.Charge(member.string_val.size());
    }
    std::fill(members.begin(), members.end(), nullptr);
    for (size_t member_index = 0; member_index < t.object_keys.size(); ++member_index) {
      const auto& key = t.object_keys[member_index];
      const auto found = parameter_indices.find(key);
      if (found == parameter_indices.end()) {
        if (policy == TestRowPolicy::kSeed) {
          error =
              std::string(field) + " " + std::to_string(i) + " has unknown parameter '" + key + "'";
          return false;
        }
        continue;
      }
      members[found->second] = &t.object_vals[member_index];
    }

    coverwise::model::TestCase tc;
    tc.values.resize(params.size(), coverwise::model::kUnassigned);

    // Filled on first drift only: a row that matches the model costs nothing,
    // and a row that does not keeps the caller's own text for the diagnostic.
    auto record_unresolved = [&tc, &params](size_t pi, std::string text) {
      if (tc.unresolved.empty()) tc.unresolved.resize(params.size());
      tc.unresolved[pi] = std::move(text);
    };

    for (size_t pi = 0; pi < params.size(); ++pi) {
      const JsonValue* val = members[pi];
      if (val == nullptr || val->IsNull()) {
        if (policy == TestRowPolicy::kRecorded) continue;
        error = std::string(field) + " " + std::to_string(i) + " missing parameter '" +
                params[pi].name + "'";
        return false;
      }
      // Convert value to string for lookup.
      std::string val_str;
      if (val->type == JsonType::kString) {
        val_str = val->string_val;
      } else if (val->type == JsonType::kNumber) {
        val_str = coverwise::util::JsNumberToString(val->number_val);
      } else if (val->type == JsonType::kBool) {
        val_str = val->bool_val ? "true" : "false";
      } else {
        // A non-scalar member is structurally malformed on every policy: there
        // is no value to record, drifted or otherwise.
        error = std::string(field) + " " + std::to_string(i) + " parameter '" + params[pi].name +
                "' has non-scalar value";
        return false;
      }

      // Find the value index (checking primary values and aliases).
      uint32_t val_idx = coverwise::model::ResolveValueName(params[pi], val_str);
      if (val_idx == coverwise::model::kUnassigned) {
        if (policy == TestRowPolicy::kRecorded) {
          record_unresolved(pi, std::move(val_str));
          continue;
        }
        error = std::string(field) + " " + std::to_string(i) + " parameter '" + params[pi].name +
                "' has unknown value '" + val_str + "'";
        return false;
      }
      tc.values[pi] = val_idx;
    }
    tests.push_back(std::move(tc));
  }
  return true;
}

/// @brief Accept a bare tests array or the schema-v1 generation envelope.
///
/// Analyze and extend are documented to consume `generate` output directly.
/// Keep the envelope checks strict so a malformed or future schema is never
/// silently interpreted as an empty test suite.
bool ExtractTestsArray(const JsonValue& json, const JsonValue*& tests, std::string& error) {
  if (json.type == JsonType::kArray) {
    tests = &json;
    return true;
  }
  if (json.type != JsonType::kObject) {
    error = "tests must be a JSON array or a schema-v1 generation result";
    return false;
  }
  if (!json.HasKey("schemaVersion") || json["schemaVersion"].type != JsonType::kNumber ||
      json["schemaVersion"].number_val != 1.0) {
    error = "tests result must have schemaVersion 1";
    return false;
  }
  if (!json.HasKey("tests") || json["tests"].type != JsonType::kArray) {
    error = "tests result must have a 'tests' JSON array";
    return false;
  }
  tests = &json["tests"];
  return true;
}

/// @brief Parse a JSON array of constraint strings into expression strings.
/// @return true on success; on failure sets error and returns false.
bool ParseConstraintExpressions(const JsonValue& json, std::vector<std::string>& expressions,
                                std::string& error) {
  if (json.IsNull()) return true;
  if (json.type != JsonType::kArray) {
    error = "constraints must be a JSON array";
    return false;
  }
  if (json.array_val.size() > kMaxConstraints) {
    error = "constraints exceed maximum of " + std::to_string(kMaxConstraints);
    return false;
  }
  for (size_t i = 0; i < json.array_val.size(); ++i) {
    const auto& c = json.array_val[i];
    if (c.type != JsonType::kString) {
      error = "constraint " + std::to_string(i) + " must be a string";
      return false;
    }
    expressions.push_back(c.string_val);
  }
  return true;
}

namespace {

/// @brief Parse sub-models from a JSON array of {parameters, strength} objects.
bool ParseSubModels(const JsonValue& json, std::vector<coverwise::model::SubModel>& sub_models,
                    std::string& error) {
  if (json.IsNull()) return true;
  if (json.type != JsonType::kArray) {
    error = "subModels must be a JSON array";
    return false;
  }
  for (size_t i = 0; i < json.array_val.size(); ++i) {
    const auto& sm = json.array_val[i];
    if (sm.type != JsonType::kObject) {
      error = "subModel " + std::to_string(i) + " must be an object";
      return false;
    }
    coverwise::model::SubModel sub_model;
    const auto& sm_strength = sm["strength"];
    if (sm_strength.type != JsonType::kNumber ||
        sm_strength.number_val != std::floor(sm_strength.number_val) ||
        sm_strength.number_val < 1 || sm_strength.number_val > 4294967295.0) {
      error = "subModel " + std::to_string(i) + " strength must be a positive integer";
      return false;
    }
    sub_model.strength = static_cast<uint32_t>(sm_strength.number_val);
    const auto& sm_params = sm["parameters"];
    if (sm_params.type != JsonType::kArray || sm_params.array_val.empty()) {
      error = "subModel " + std::to_string(i) + " parameters must be a non-empty array";
      return false;
    }
    for (const auto& p : sm_params.array_val) {
      if (p.type != JsonType::kString) {
        error = "subModel " + std::to_string(i) + " parameter names must be strings";
        return false;
      }
      sub_model.parameter_names.push_back(p.string_val);
    }
    sub_models.push_back(std::move(sub_model));
  }
  return true;
}

/// @brief Parse value weights from JSON: {"param": {"value": weight, ...}, ...}.
/// @return true on success; on failure sets error and returns false.
bool ParseWeights(const JsonValue& json, coverwise::model::WeightConfig& weights,
                  std::string& error) {
  if (json.IsNull()) return true;
  if (json.type != JsonType::kObject) {
    error = "weights must be a JSON object";
    return false;
  }
  for (size_t i = 0; i < json.object_keys.size(); ++i) {
    const auto& param_name = json.object_keys[i];
    const auto& param_weights = json.object_vals[i];
    if (param_weights.type != JsonType::kObject) {
      error = "weights for '" + param_name + "' must be an object";
      return false;
    }
    for (size_t j = 0; j < param_weights.object_keys.size(); ++j) {
      const auto& value_name = param_weights.object_keys[j];
      const auto& weight_val = param_weights.object_vals[j];
      if (weight_val.type != JsonType::kNumber) {
        error = "weight for '" + param_name + "." + value_name + "' must be a number";
        return false;
      }
      weights.entries[param_name][value_name] = weight_val.number_val;
    }
  }
  return true;
}

}  // namespace

/// @brief Read the top-level model document into generation options.
///
/// `generate`, `extend` and `stats` all consume the same document, and `stats`
/// is documented as a preflight for `generate`: whatever one of them accepts,
/// the others have to accept too, and reject with the same message. Reading
/// every top-level field in one place is what keeps that true — a field wired
/// into a single subcommand cannot silently be missed by the rest.
///
/// `seeds` are held to the seed policy here even for `stats`, which reports no
/// figure derived from them: the acceptance decision is the whole point of a
/// preflight, so a seed row generation would refuse must not pass.
/// @param budget Row text charged so far in this invocation, which `seeds` draws
///        from alongside whatever row array the subcommand reads separately.
/// @return an ok Error on success, otherwise the failure to surface.
coverwise::model::Error ParseModelDocument(const JsonValue& json,
                                           coverwise::model::GenerateOptions& options,
                                           coverwise::model::ChargedTextReader& budget) {
  // Parameters are parsed and their boundary value space expanded first, so
  // that everything below resolves value names against the value space
  // generation actually uses.
  if (auto error = ParseModelParameters(json["parameters"], options); !error.ok()) return error;

  std::string error;
  // Optional scalar fields.
  if (!ParseOptionalUint32(json, "strength", 1, options.strength, error)) {
    return ReaderError(std::move(error));
  }
  uint32_t seed = 0;
  if (!ParseOptionalUint32(json, "seed", 0, seed, error)) return ReaderError(std::move(error));
  options.seed = seed;
  if (!ParseOptionalUint32(json, "maxTests", 0, options.max_tests, error)) {
    return ReaderError(std::move(error));
  }

  // Constraints (array of strings).
  if (!ParseConstraintExpressions(json["constraints"], options.constraint_expressions, error)) {
    return ReaderError(std::move(error));
  }

  // Weights: {"param_name": {"value_name": weight, ...}, ...}
  if (!ParseWeights(json["weights"], options.weights, error)) return ReaderError(std::move(error));

  // Sub-models (mixed-strength parameter groups).
  if (!ParseSubModels(json["subModels"], options.sub_models, error)) {
    return ReaderError(std::move(error));
  }

  // Seed tests (existing tests to build upon). Their value indices point into
  // the already-expanded value lists.
  const auto& seeds_val = json["seeds"];
  if (!seeds_val.IsNull() && !ParseTests(seeds_val, options.parameters, TestRowPolicy::kSeed,
                                         "seeds", options.seeds, budget, error)) {
    return ReaderError(std::move(error));
  }
  return {};
}

}  // namespace cli
}  // namespace coverwise
