/// @file bindings.cpp
/// @brief Emscripten embind bindings for coverwise WASM module.
///
/// Design: JSON parsing is done on the JS side. The WASM module receives and
/// returns JavaScript objects (emscripten::val). The JS wrapper is thin — it
/// just calls the WASM function with a JS object and gets a JS object back.

#ifdef __EMSCRIPTEN__

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "binding/wasm/js_value.h"
#include "core/generator.h"
#include "model/boundary.h"
#include "model/constraint_parser.h"
#include "model/error.h"
#include "model/limits.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "validator/coverage_validator.h"

using namespace emscripten;

namespace {

using coverwise::binding::JsArray;
using coverwise::binding::JsObject;
using coverwise::binding::JsScalar;
using coverwise::binding::JsValue;

// ---------------------------------------------------------------------------
// JS -> C++ conversion helpers
//
// Nothing below reads a field out of a caller-supplied object directly. Input
// arrives as one of the wrappers in js_value.h, which establishes a value's
// shape before it can be used and rejects everything else as a
// std::runtime_error — caught at the boundary and reported as INVALID_INPUT.
// Under DISABLE_EXCEPTION_CATCHING a raw emscripten type error would instead
// abort the module and poison the singleton, so no JS exception may escape.
// ---------------------------------------------------------------------------

/// @brief A structured failure raised while reading input.
///
/// Reading input reports two kinds of failure. A malformed shape is the
/// binding's own finding and has nothing but text to offer, so it travels as a
/// std::runtime_error and is reported as INVALID_INPUT. A rule from the model
/// layer already carries a code and a detail, and this type is how those reach
/// the boundary intact.
///
/// Rendering such a failure to text here would lose both: the code, because
/// every text failure is reported under the one the boundary assigns, and the
/// detail, because composing it into the message leaves the caller a `detail`
/// field it can no longer read. Both losses are silent, and neither is visible
/// to a scan of the source.
///
/// It derives from std::exception so that a boundary which somehow fails to
/// name it still contains it: an exception escaping into embind would abort the
/// module. `what()` is deliberately not overridden — the text belongs to the
/// surface that renders the error, not to the exception carrying it.
struct InputError : std::exception {
  explicit InputError(coverwise::model::Error error) : error(std::move(error)) {}

  coverwise::model::Error error;
};

/// @brief Caller-supplied row text charged so far in one call.
///
/// The documented aggregate limit bounds the strings a single call hands the
/// engine, and a row array is the largest of them: a row arrives as value names
/// and reaches the engine as value indices, so nothing downstream can charge it.
///
/// What counts is the caller's own text. A row's keys are parameter names,
/// which the model charges once each already — charging them again per row
/// would make the budget shrink with the parameter count rather than bound what
/// the caller supplied. Numbers and booleans are rendered by the engine rather
/// than supplied as text, so they cost nothing.
///
/// One accumulator per call: extend reads both `existing` and `seeds`, and two
/// half-sized suites are the same dimension of input as one full-sized one. The
/// total goes to the acceptance gate, which owns the limit and its wording —
/// this layer counts, it does not judge.
struct RowStringBudget {
  size_t charged = 0;
};

/// @brief Rejection text for a field that has to be a number.
std::string NumberMessage(const char* field) {
  return std::string("Invalid ") + field + ": must be a number";
}

/// @brief Read a top-level object argument, naming the argument it came from.
JsObject RequireObjectArgument(val raw, const char* field) {
  return JsValue(std::move(raw))
      .RequireObject(std::string("Invalid ") + field + ": must be an object.");
}

/// @brief Read a top-level array argument, naming the argument it came from.
JsArray RequireArrayArgument(val raw, const char* field) {
  return JsValue(std::move(raw))
      .RequireArray(std::string("Invalid ") + field + ": must be an array.");
}

/// @brief Validate a JS number as a uint32 with public API validation.
///
/// Rejects non-numbers, non-finite values, fractional values, negatives, and
/// values beyond uint32 range. When @p allow_zero is false, zero is also rejected.
uint32_t ParseUint32Scalar(const JsScalar& raw, const char* field, bool allow_zero) {
  double d = raw.RequireNumber(NumberMessage(field));
  if (!std::isfinite(d) || std::floor(d) != d || d < 0.0 ||
      d > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error(std::string("Invalid ") + field + ": must be a non-negative integer");
  }
  if (!allow_zero && d == 0.0) {
    throw std::runtime_error(std::string("Invalid ") + field + ": must be a positive integer");
  }
  return static_cast<uint32_t>(d);
}

/// @brief Rejection text shared by every surface that reads constraints.
const char kConstraintsMessage[] = "Invalid constraints: must be an array of strings.";

/// @brief Append the expressions of an optional constraint array to @p expressions.
void AppendConstraintExpressions(const std::optional<JsArray>& js_constraints,
                                 std::vector<std::string>& expressions) {
  if (!js_constraints) return;
  const uint32_t count = js_constraints->size();
  expressions.reserve(expressions.size() + count);
  for (uint32_t i = 0; i < count; ++i) {
    expressions.push_back(js_constraints->At(i)
                              .RequireScalar(kConstraintsMessage)
                              .RequireString(kConstraintsMessage));
  }
}

std::optional<coverwise::model::BoundaryConfig> ParseBoundaryConfigForParam(
    const JsObject& js_param, const std::string& name);

/// @brief Parse a single JS parameter object into a C++ Parameter.
///
/// Handles three value formats:
///   - Simple string: "chrome"
///   - Object with value: { value: "ie6", invalid: true }
///   - Object with aliases: { value: "chromium", aliases: ["chrome", "edge"] }
///
/// Every value pushed is one the caller wrote: a scalar element, or the scalar
/// `value` member of an object element. An element of any other shape is
/// rejected before a single value is pushed, so a suite is never generated over
/// a value the caller never described.
///
/// A boundary config found on the parameter is recorded in @p boundary_configs
/// rather than applied here: expansion belongs to the acceptance gate, which
/// runs it over the whole model before judging it.
coverwise::model::Parameter ParseParameter(
    const JsObject& js_param,
    std::map<std::string, coverwise::model::BoundaryConfig>& boundary_configs) {
  coverwise::model::Parameter param;

  const std::string name_message = "Parameter name must be a non-empty string";
  param.name = js_param.RequireScalar("name", name_message).RequireString(name_message);

  const JsArray js_values = js_param.RequireArray(
      "values", "Parameter '" + param.name + "' must have at least one value");
  const uint32_t count = js_values.size();

  std::vector<bool> invalid_flags;
  std::vector<std::vector<std::string>> aliases;
  std::vector<std::string> eq_classes;
  bool has_invalid = false;
  bool has_aliases = false;
  bool has_classes = false;

  const val string_ctor = val::global("String");
  for (uint32_t i = 0; i < count; ++i) {
    const JsValue item = js_values.At(i);
    if (auto scalar = item.TryScalar()) {
      // Scalar value — convert to string
      param.values.push_back(scalar->ToText(string_ctor));
      invalid_flags.push_back(false);
      aliases.emplace_back();
      eq_classes.emplace_back();
      continue;
    }

    // Object form: { value: "...", invalid?: bool, aliases?: [...], class?: "..." }
    const std::string where = param.name + "[" + std::to_string(i) + "]";
    const std::string value_message =
        "Invalid value at " + where + ": expected string, number, or boolean.";
    const JsObject entry = item.RequireObject(value_message);
    param.values.push_back(entry.RequireScalar("value", value_message).ToText(string_ctor));

    const std::string flag_message = "Invalid flag at " + where + " must be boolean.";
    bool is_invalid = false;
    if (auto js_invalid = entry.OptionalScalar("invalid", flag_message)) {
      is_invalid = js_invalid->RequireBool(flag_message);
    }
    invalid_flags.push_back(is_invalid);
    if (is_invalid) has_invalid = true;

    const std::string alias_message = "Aliases at " + where + " must be non-empty strings.";
    std::vector<std::string> value_aliases;
    if (auto js_aliases = entry.OptionalArray("aliases", alias_message)) {
      const uint32_t alias_count = js_aliases->size();
      value_aliases.reserve(alias_count);
      for (uint32_t a = 0; a < alias_count; ++a) {
        value_aliases.push_back(js_aliases->RequireNonEmptyStringAt(a, alias_message));
      }
      if (!value_aliases.empty()) has_aliases = true;
    }
    aliases.push_back(std::move(value_aliases));

    // Equivalence class (non-empty string).
    const std::string class_message = "Class at " + where + " must be a string.";
    std::string eq_class;
    if (auto js_class = entry.OptionalScalar("class", class_message)) {
      eq_class = js_class->RequireString(class_message);
    }
    if (!eq_class.empty()) has_classes = true;
    eq_classes.push_back(std::move(eq_class));
  }

  if (has_invalid) {
    param.set_invalid(std::move(invalid_flags));
  }
  if (has_aliases) {
    param.set_aliases(std::move(aliases));
  }
  if (has_classes) {
    param.set_equivalence_classes(std::move(eq_classes));
  }

  auto boundary = ParseBoundaryConfigForParam(js_param, param.name);
  if (boundary) {
    boundary_configs[param.name] = *boundary;
  }
  return param;
}

/// @brief Derive a boundary expansion config from a single JS parameter object.
///
/// A parameter opts in by carrying any of `type` / `range` / `step`; having
/// opted in it must supply a `type` of "integer" or "float" and a 2-element
/// numeric `range`, with an optional numeric `step`. A malformed shape is an
/// error rather than an opt-out — degrading to "no expansion" would generate
/// over a value space the caller never described. std::nullopt means only that
/// the parameter carries no boundary fields at all. Mirrors the CLI's
/// ParseBoundaryConfigs and the pure-JS boundaryConfigFromParam.
std::optional<coverwise::model::BoundaryConfig> ParseBoundaryConfigForParam(
    const JsObject& js_param, const std::string& name) {
  // Opting in is a question about the keys the caller wrote, so it is the one
  // place a key is asked about; every value below still comes through an
  // accessor that establishes its shape.
  if (!js_param.HasField("type") && !js_param.HasField("range") && !js_param.HasField("step")) {
    return std::nullopt;
  }

  coverwise::model::BoundaryConfig config;
  const std::string type_message = "Invalid boundary type for parameter '" + name + "'";
  std::string type;
  if (auto js_type = js_param.OptionalScalar("type", type_message)) {
    if (js_type->IsString()) type = js_type->RequireString(type_message);
  }
  if (type == "integer") {
    config.type = coverwise::model::BoundaryConfig::Type::kInteger;
  } else if (type == "float") {
    config.type = coverwise::model::BoundaryConfig::Type::kFloat;
  } else {
    throw std::runtime_error(type_message);
  }

  const std::string range_message =
      "Invalid boundary range for parameter '" + name + "': expected finite [min, max]";
  auto js_range = js_param.OptionalArray("range", range_message);
  if (!js_range || js_range->size() != 2) {
    throw std::runtime_error(range_message);
  }
  config.min_value = js_range->At(0).RequireScalar(range_message).RequireNumber(range_message);
  config.max_value = js_range->At(1).RequireScalar(range_message).RequireNumber(range_message);

  // `step` is carried through for both types, including integer, where the
  // acceptance rules reject anything other than 1.
  const std::string step_message =
      "Invalid boundary step for parameter '" + name + "': expected a positive finite number";
  if (auto js_step = js_param.OptionalScalar("step", step_message)) {
    config.step = js_step->RequireNumber(step_message);
  } else {
    config.step = 1.0;
  }
  return config;
}

/// @brief Parse a JS parameters array into @p options and expand its values.
///
/// Expansion runs before anything resolves a value name to an index, and before
/// the parameter set is judged, so the rules apply to the value space the engine
/// will use. Mirrors the CLI's ParseModelParameters.
void ParseModelParameters(const JsArray& js_params, coverwise::model::GenerateOptions& options) {
  const uint32_t count = js_params.size();
  options.parameters.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const JsObject js_param = js_params.At(i).RequireObject(
        "Invalid parameter at index " + std::to_string(i) + ": must be an object.");
    options.parameters.push_back(ParseParameter(js_param, options.boundary_configs));
  }
  // These two rules belong to the model layer, so their findings travel to the
  // boundary as they were made rather than as text: the caller reads the same
  // code and the same detail it would from any other surface.
  auto expansion = coverwise::model::ExpandBoundaries(options);
  if (!expansion.ok()) {
    throw InputError(std::move(expansion));
  }
  // Semantic checks (duplicate names/values, empty values) shared with the
  // pure-JS surface and the CLI via the model layer.
  auto err = coverwise::model::ValidateParameters(options.parameters);
  if (!err.ok()) {
    throw InputError(std::move(err));
  }
}

/// @brief State a row conversion borrows, built once for a whole suite.
///
/// Both members exist to keep the per-row cost linear in the row's own size.
/// Resolving a key by scanning the parameter vector costs O(keys x params) per
/// row, which is the dominant term of a large suite; and `val::global` resolves
/// a name against the JS global object on every call, which a per-cell lookup
/// pays once per value converted.
struct RowConversionContext {
  /// First entry wins, matching the scan it replaces: rows are converted before
  /// the acceptance gate has ruled out a duplicate parameter name.
  std::unordered_map<std::string, uint32_t> param_index;
  val object_ctor;
  val string_ctor;

  explicit RowConversionContext(const std::vector<coverwise::model::Parameter>& params)
      : object_ctor(val::global("Object")), string_ctor(val::global("String")) {
    param_index.reserve(params.size());
    for (uint32_t i = 0; i < params.size(); ++i) {
      param_index.emplace(params[i].name, i);
    }
  }
};

/// @brief Parse a JS test case object into a C++ TestCase using parameter definitions.
///
/// The JS test case is a map of param_name -> value_string. We resolve each
/// value to its index using find_value_index. A key naming no parameter is
/// skipped, as it was when the parameter vector was scanned for it.
///
/// @param field The caller's own name for the array this row came from, so a
///        rejected cell names the field the caller wrote.
coverwise::model::TestCase ParseTestCase(const JsObject& js_test,
                                         const std::vector<coverwise::model::Parameter>& params,
                                         const RowConversionContext& ctx, bool allow_unknown,
                                         const char* field, uint32_t row_index,
                                         RowStringBudget& budget) {
  coverwise::model::TestCase tc;
  tc.values.resize(params.size(), coverwise::model::kUnassigned);

  js_test.ForEachEntry(ctx.object_ctor, [&](const std::string& key, const JsValue& cell) {
    auto it = ctx.param_index.find(key);
    if (it == ctx.param_index.end()) return;
    const uint32_t i = it->second;
    // The message is composed only for a cell that is being rejected: building
    // one per cell would cost more than the check it describes.
    auto scalar = cell.TryScalar();
    if (!scalar) {
      throw std::runtime_error("Invalid " + std::string(field) + "[" + std::to_string(row_index) +
                               "]." + key + ": expected string, number, or boolean.");
    }
    std::string val_str = scalar->ToText(ctx.string_ctor);
    // Charged before it is resolved: what the limit bounds is the text the
    // caller handed over, whether or not the model has a value to match it to.
    if (scalar->IsString()) budget.charged += val_str.size();
    uint32_t idx = params[i].find_value_index(val_str);
    if (idx == UINT32_MAX) {
      if (allow_unknown) {
        // Filled on first drift only: a row that matches the model costs
        // nothing, and a row that does not keeps the caller's own text so
        // the diagnostic can name it instead of an internal index.
        if (tc.unresolved.empty()) tc.unresolved.resize(params.size());
        tc.unresolved[i] = std::move(val_str);
        return;
      }
      throw std::runtime_error("Unknown value '" + val_str + "' for parameter '" + params[i].name +
                               "'");
    }
    tc.values[i] = idx;
  });
  return tc;
}

/// @brief Parse a JS array of test case objects into C++ TestCase vector.
///
/// The documented row ceiling is enforced here rather than by each caller, and
/// independently of the JS wrapper: an embedder calling the compiled module
/// directly must meet the same bound.
std::vector<coverwise::model::TestCase> ParseTestCases(
    const JsArray& js_tests, const std::vector<coverwise::model::Parameter>& params,
    const char* field, RowStringBudget& budget, bool allow_unknown = false) {
  const uint32_t count = js_tests.size();
  if (count > coverwise::model::kMaxTests) {
    throw std::runtime_error("Invalid " + std::string(field) + ": maximum is " +
                             std::to_string(coverwise::model::kMaxTests) + " rows.");
  }
  std::vector<coverwise::model::TestCase> tests;
  tests.reserve(count);
  const RowConversionContext ctx(params);
  for (uint32_t i = 0; i < count; ++i) {
    auto row = js_tests.At(i).TryObject();
    if (!row) {
      throw std::runtime_error("Invalid " + std::string(field) + "[" + std::to_string(i) +
                               "]: must be an object.");
    }
    tests.push_back(ParseTestCase(*row, params, ctx, allow_unknown, field, i, budget));
  }
  return tests;
}

/// @brief Parse weight configuration from JS object.
///
/// Expected format: { "os": { "win": 2.0, "mac": 1.5 }, "browser": { ... } }
coverwise::model::WeightConfig ParseWeights(const JsObject& js_weights) {
  coverwise::model::WeightConfig weights;
  const val object_ctor = val::global("Object");
  js_weights.ForEachEntry(object_ctor, [&](const std::string& param_name, const JsValue& entry) {
    const JsObject param_weights =
        entry.RequireObject("Invalid weights." + param_name + ": must be an object.");
    param_weights.ForEachEntry(object_ctor, [&](const std::string& value_name, const JsValue& raw) {
      const std::string message =
          "Invalid weight for " + param_name + "=" + value_name + ": must be finite and positive.";
      const double weight = raw.RequireScalar(message).RequireNumber(message);
      if (!std::isfinite(weight) || weight <= 0.0) throw std::runtime_error(message);
      weights.entries[param_name][value_name] = weight;
    });
  });
  return weights;
}

/// @brief Parse sub-models from JS array.
///
/// Expected format: [{ parameters: ["os", "browser"], strength: 3 }, ...]
std::vector<coverwise::model::SubModel> ParseSubModels(const JsArray& js_sub_models) {
  std::vector<coverwise::model::SubModel> sub_models;
  const uint32_t count = js_sub_models.size();
  sub_models.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    const std::string message = "Invalid subModels[" + std::to_string(i) + "].";
    const JsObject js_sm = js_sub_models.At(i).RequireObject(message);
    coverwise::model::SubModel sm;
    const JsArray js_names = js_sm.RequireArray("parameters", message);
    const uint32_t name_count = js_names.size();
    sm.parameter_names.reserve(name_count);
    for (uint32_t j = 0; j < name_count; ++j) {
      sm.parameter_names.push_back(js_names.RequireNonEmptyStringAt(j, message));
    }
    if (auto js_strength = js_sm.OptionalScalar("strength", message)) {
      // Validate as a positive integer; a fractional or zero strength would
      // otherwise be silently truncated (e.g. 0 → no-op sub-model).
      const double strength = js_strength->RequireNumber(message);
      if (!std::isfinite(strength) || std::floor(strength) != strength || strength < 1.0 ||
          strength > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
        throw std::runtime_error(message);
      }
      sm.strength = static_cast<uint32_t>(strength);
    }
    sub_models.push_back(std::move(sm));
  }
  return sub_models;
}

/// @brief Read a JS input object into a GenerateOptions, without judging it.
///
/// Reading and judging are separate because a caller may hand over row text
/// that this object does not carry — extend supplies its existing suite
/// alongside the input — and the whole of one call is judged against one
/// budget. The caller reads, charges what it read into @p budget, and then
/// submits the result to the gate.
coverwise::model::GenerateOptions ReadGenerateOptions(const JsObject& input,
                                                      RowStringBudget& budget) {
  coverwise::model::GenerateOptions opts;

  // Parameters (required)
  ParseModelParameters(input.RequireArray("parameters"), opts);

  // Strength (default 2)
  if (auto js_strength = input.OptionalScalar("strength", NumberMessage("strength"))) {
    opts.strength = ParseUint32Scalar(*js_strength, "strength", false);
  }

  // Seed (default 0). Canonical domain: integer in [0, 2^32 - 1].
  const std::string seed_message = "Invalid seed: must be an integer in [0, 4294967295]";
  if (auto js_seed = input.OptionalScalar("seed", seed_message)) {
    // JS numbers are doubles; validate before casting to avoid UB on negative
    // values or silent wraparound on out-of-range values.
    const double seed_d = js_seed->RequireNumber(seed_message);
    if (seed_d != std::floor(seed_d) || seed_d < 0.0 || seed_d > 4294967295.0) {
      throw std::runtime_error(seed_message);
    }
    opts.seed = static_cast<uint64_t>(seed_d);
  }

  // Max tests (default 0 = no limit)
  if (auto js_max_tests = input.OptionalScalar("maxTests", NumberMessage("maxTests"))) {
    opts.max_tests = ParseUint32Scalar(*js_max_tests, "maxTests", true);
  }

  // Constraint expressions (strings)
  AppendConstraintExpressions(input.OptionalArray("constraints", kConstraintsMessage),
                              opts.constraint_expressions);

  // Seed tests (existing tests to build upon)
  if (auto js_seeds = input.OptionalArray("seeds")) {
    opts.seeds = ParseTestCases(*js_seeds, opts.parameters, "seeds", budget);
  }

  // Weights
  if (auto js_weights = input.OptionalObject("weights")) {
    opts.weights = ParseWeights(*js_weights);
  }

  // Sub-models
  if (auto js_sub_models = input.OptionalArray("subModels")) {
    opts.sub_models = ParseSubModels(*js_sub_models);
  }

  // ParseModelParameters already expanded the boundary parameters and cleared
  // the configs, so opts.parameters is the final value space and core::Generate
  // will not expand a second time.
  return opts;
}

// ---------------------------------------------------------------------------
// C++ -> JS conversion helpers
// ---------------------------------------------------------------------------

/// @brief Convert a test case to a JS object with param_name -> value_string.
///
/// Uses display_name() for alias rotation.
val TestCaseToJS(const coverwise::model::TestCase& tc,
                 const std::vector<coverwise::model::Parameter>& params, uint32_t rotation) {
  val obj = val::global("Object").call<val>("create", val::null());
  for (uint32_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
    if (tc.values[i] != coverwise::model::kUnassigned && tc.values[i] < params[i].size()) {
      obj.set(params[i].name, params[i].display_name(tc.values[i], rotation));
    }
  }
  return obj;
}

/// @brief Convert a vector of test cases to a JS array.
///
/// @param preserved_rows Rows the caller handed to extend, or undefined. Extend
///        keeps them exactly as supplied, so they are echoed rather than
///        rendered from value indices: rendering would substitute the primary
///        value for an alias the caller wrote, and drop members the model no
///        longer declares. Echoing here, where a result becomes a JS object, is
///        what keeps the rule from having to be re-applied by each entry point.
val TestCasesToJS(const std::vector<coverwise::model::TestCase>& tests,
                  const std::vector<coverwise::model::Parameter>& params,
                  val preserved_rows = val::undefined()) {
  const uint32_t preserved_count = val::global("Array").call<bool>("isArray", preserved_rows)
                                       ? preserved_rows["length"].as<uint32_t>()
                                       : 0;
  val arr = val::array();
  for (uint32_t i = 0; i < tests.size(); ++i) {
    arr.call<void>("push",
                   i < preserved_count ? preserved_rows[i] : TestCaseToJS(tests[i], params, i));
  }
  return arr;
}

/// @brief Convert uncovered tuples to a JS array.
val UncoveredToJS(const std::vector<coverwise::model::UncoveredTuple>& uncovered) {
  val arr = val::array();
  for (const auto& ut : uncovered) {
    val obj = val::object();

    val tuple_arr = val::array();
    for (const auto& s : ut.tuple) {
      tuple_arr.call<void>("push", val(s));
    }
    obj.set("tuple", tuple_arr);

    val params_arr = val::array();
    for (const auto& p : ut.params) {
      params_arr.call<void>("push", val(p));
    }
    obj.set("params", params_arr);

    val indices_arr = val::array();
    for (const auto& [parameter_index, value_index] : ut.indices) {
      val index_pair = val::array();
      index_pair.call<void>("push", parameter_index);
      index_pair.call<void>("push", value_index);
      indices_arr.call<void>("push", index_pair);
    }
    obj.set("indices", indices_arr);

    obj.set("reason", ut.reason);
    obj.set("display", val(ut.ToString()));

    arr.call<void>("push", obj);
  }
  return arr;
}

/// @brief Convert a GenerateResult to a JS object.
val GenerateResultToJS(const coverwise::model::GenerateResult& result,
                       const std::vector<coverwise::model::Parameter>& params, uint32_t strength,
                       val preserved_rows = val::undefined()) {
  val obj = val::object();

  // strength
  obj.set("strength", val(strength));

  // tests
  obj.set("tests", TestCasesToJS(result.tests, params, preserved_rows));

  // negativeTests
  obj.set("negativeTests", TestCasesToJS(result.negative_tests, params));
  if (result.negative_coverage) {
    val negative_coverage = val::object();
    negative_coverage.set("totalTuples",
                          static_cast<double>(result.negative_coverage->total_tuples));
    negative_coverage.set("coveredTuples",
                          static_cast<double>(result.negative_coverage->covered_tuples));
    negative_coverage.set("omittedTuples",
                          static_cast<double>(result.negative_coverage->omitted_tuples));
    negative_coverage.set("coverageRatio", result.negative_coverage->coverage_ratio);
    obj.set("negativeCoverage", negative_coverage);
  }

  // coverage
  obj.set("coverage", result.coverage);

  // uncovered
  obj.set("uncovered", UncoveredToJS(result.uncovered));
  obj.set("uncoveredCount", static_cast<double>(result.uncovered_count));
  obj.set("omittedUncovered", static_cast<double>(result.omitted_uncovered));

  // stats
  val stats = val::object();
  stats.set("totalTuples", static_cast<double>(result.stats.total_tuples));
  stats.set("coveredTuples", static_cast<double>(result.stats.covered_tuples));
  stats.set("testCount", result.stats.test_count);
  obj.set("stats", stats);

  // suggestions
  val suggestions = val::array();
  for (uint32_t i = 0; i < result.suggestions.size(); ++i) {
    val s = val::object();
    s.set("description", result.suggestions[i].description);
    // Suggestions are canonical diagnostics, not rotated test-suite rows.
    // Keep their text and object values self-consistent across WASM and Pure.
    s.set("testCase", TestCaseToJS(result.suggestions[i].test_case, params, 0));
    suggestions.call<void>("push", s);
  }
  obj.set("suggestions", suggestions);

  // warnings
  val warnings = val::array();
  for (const auto& w : result.warnings) {
    warnings.call<void>("push", val(w));
  }
  obj.set("warnings", warnings);

  // classCoverage (if available)
  if (result.class_coverage) {
    val cc = val::object();
    cc.set("totalClassTuples", static_cast<double>(result.class_coverage->total_class_tuples));
    cc.set("coveredClassTuples", static_cast<double>(result.class_coverage->covered_class_tuples));
    cc.set("classCoverageRatio", result.class_coverage->class_coverage_ratio);
    obj.set("classCoverage", cc);
  }

  return obj;
}

/// @brief Convert a CoverageReport to a JS object.
val CoverageReportToJS(const coverwise::validator::CoverageReport& report) {
  val obj = val::object();
  obj.set("totalTuples", static_cast<double>(report.total_tuples));
  obj.set("coveredTuples", static_cast<double>(report.covered_tuples));
  obj.set("coverageRatio", report.coverage_ratio);
  obj.set("uncovered", UncoveredToJS(report.uncovered));
  obj.set("uncoveredCount", static_cast<double>(report.uncovered_count));
  obj.set("omittedUncovered", static_cast<double>(report.omitted_uncovered));
  val invalid_tests = val::array();
  for (const auto& invalid : report.invalid_tests) {
    val item = val::object();
    item.set("testIndex", invalid.test_index);
    item.set("reason", invalid.reason);
    invalid_tests.call<void>("push", item);
  }
  obj.set("invalidTests", invalid_tests);
  return obj;
}

/// @brief Convert ModelStats to a JS object.
val ModelStatsToJS(const coverwise::model::ModelStats& stats) {
  val obj = val::object();
  obj.set("parameterCount", stats.parameter_count);
  obj.set("totalValues", stats.total_values);
  obj.set("strength", stats.strength);
  obj.set("totalTuples", static_cast<double>(stats.total_tuples));
  obj.set("estimatedTests", stats.estimated_tests);
  obj.set("subModelCount", stats.sub_model_count);
  obj.set("constraintCount", stats.constraint_count);

  val params_arr = val::array();
  for (const auto& pd : stats.parameters) {
    val p = val::object();
    p.set("name", pd.name);
    p.set("valueCount", pd.value_count);
    p.set("invalidCount", pd.invalid_count);
    params_arr.call<void>("push", p);
  }
  obj.set("parameters", params_arr);

  return obj;
}

/// @brief Create a JS error object carrying a numeric model::Error::Code.
///
/// The JS wrapper maps this number to a canonical string code. The default 3
/// (kInvalidInput) covers exceptions thrown at the JS<->C++ boundary (parse and
/// shape errors); structured core failures pass their real code explicitly.
val MakeError(const std::string& message, int code = 3, const std::string& detail = {}) {
  val obj = val::object();
  obj.set("error", true);
  obj.set("code", code);
  obj.set("message", val(message));
  obj.set("detail", val(detail));
  return obj;
}

/// @brief Build a JS error object from a structured core Error.
val MakeError(const coverwise::model::Error& error) {
  return MakeError(error.message, static_cast<int>(error.code), error.detail);
}

// ---------------------------------------------------------------------------
// Exported WASM functions
// ---------------------------------------------------------------------------

/// @brief Generate a covering array from a JS input object.
///
/// @param input JS object with: parameters, constraints?, strength?, seed?,
///              maxTests?, seeds?, weights?, subModels?
/// @return JS object with: tests, negativeTests, coverage, uncovered, stats,
///         suggestions, warnings. On error: { error: true, code, message }.
val wasmGenerate(val input) {
  try {
    RowStringBudget budget;
    auto opts_read = ReadGenerateOptions(RequireObjectArgument(std::move(input), "input"), budget);
    auto accepted = coverwise::model::AcceptOptions(std::move(opts_read), budget.charged);
    if (!accepted.ok()) {
      return MakeError(accepted.error());
    }
    const auto& opts = accepted->get();
    auto result = coverwise::core::Generate(opts);

    // Core reports early-exit failures (e.g. constraint parse errors) via
    // result.error rather than throwing. Propagate the real code to JS.
    if (!result.error.ok()) {
      return MakeError(result.error);
    }

    const auto& effective_params = result.parameters.empty() ? opts.parameters : result.parameters;
    return GenerateResultToJS(result, effective_params, opts.strength);
  } catch (const InputError& e) {
    return MakeError(e.error);
  } catch (const std::exception& e) {
    return MakeError(e.what());
  }
}

/// @brief Analyze coverage of existing tests against parameters.
///
/// @param js_params JS array of parameter definitions.
/// @param js_tests JS array of test case objects (param_name -> value_string).
/// @param js_strength Interaction strength (2 = pairwise); validated as a positive integer.
/// @return JS object with: totalTuples, coveredTuples, coverageRatio, uncovered.
val wasmAnalyzeCoverage(val js_params, val js_tests, val js_strength, val js_constraints) {
  try {
    // Validate strength here too: receiving it as a raw embind uint32 would
    // silently truncate a fractional value (e.g. 2.9 → 2) before this layer.
    uint32_t strength = ParseUint32Scalar(
        JsValue(js_strength).RequireScalar(NumberMessage("strength")), "strength", false);

    // The analysis strength is not a property of the model, so the model goes
    // to the gate at strength 1 and is judged alone; the requested strength is
    // judged by ValidateCoverage, which is fail-closed. A strength of 0, or one
    // above the parameter count, is invalid input rather than a coverage claim
    // over an empty tuple universe.
    coverwise::model::GenerateOptions model_options;
    model_options.strength = 1;
    ParseModelParameters(RequireArrayArgument(std::move(js_params), "parameters"), model_options);
    AppendConstraintExpressions(JsValue(js_constraints).OptionalArray(kConstraintsMessage),
                                model_options.constraint_expressions);

    // A row that no longer matches the model keeps its mismatching positions
    // unassigned; ValidateCoverage classifies it into invalidTests so the report
    // covers the whole suite instead of stopping at the first drifted row.
    RowStringBudget budget;
    auto tests = ParseTestCases(RequireArrayArgument(std::move(js_tests), "tests"),
                                model_options.parameters, "tests", budget, true);

    auto accepted = coverwise::model::AcceptOptions(std::move(model_options), budget.charged);
    if (!accepted.ok()) {
      return MakeError(accepted.error());
    }
    const auto& params = accepted->get().parameters;

    // Parse optional constraint expressions. Mirrors ParseGenerateOptions.
    std::vector<coverwise::model::Constraint> constraints;
    for (const auto& expr : accepted->get().constraint_expressions) {
      auto parse_result = coverwise::model::ParseConstraint(expr, params);
      if (!parse_result.error.ok()) {
        return MakeError(coverwise::model::AnnotateConstraintError(expr, parse_result.error));
      }
      constraints.push_back(std::move(parse_result.constraint));
    }

    auto report = coverwise::validator::ValidateCoverage(params, tests, strength, constraints);
    if (!report.error.ok()) {
      return MakeError(report.error);
    }
    return CoverageReportToJS(report);
  } catch (const InputError& e) {
    return MakeError(e.error);
  } catch (const std::exception& e) {
    return MakeError(e.what());
  }
}

/// @brief Extend an existing test suite with additional tests for coverage.
///
/// @param js_existing JS array of existing test case objects.
/// @param input JS input object (same format as generate).
/// @return JS object with the extended test suite (same format as generate result).
val wasmExtendTests(val js_existing, val input) {
  try {
    // Both row arrays this call reads draw on one budget, so the suite is read
    // and charged before the gate runs rather than after it has already ruled.
    RowStringBudget budget;
    auto opts_read = ReadGenerateOptions(RequireObjectArgument(std::move(input), "input"), budget);
    // A recorded suite drifts from the model it was written against, and filling
    // the gap is the point of extend, so a drifted row is carried through with
    // its mismatching positions unassigned rather than failing the call.
    auto existing = ParseTestCases(RequireArrayArgument(js_existing, "existing"),
                                   opts_read.parameters, "existing", budget, true);

    auto accepted = coverwise::model::AcceptOptions(std::move(opts_read), budget.charged);
    if (!accepted.ok()) {
      return MakeError(accepted.error());
    }
    const auto& opts = accepted->get();
    auto result = coverwise::core::Extend(existing, opts);
    if (!result.error.ok()) {
      return MakeError(result.error);
    }
    const auto& effective_params = result.parameters.empty() ? opts.parameters : result.parameters;
    return GenerateResultToJS(result, effective_params, opts.strength, js_existing);
  } catch (const InputError& e) {
    return MakeError(e.error);
  } catch (const std::exception& e) {
    return MakeError(e.what());
  }
}

/// @brief Estimate model statistics without running generation.
///
/// @param input JS input object (same format as generate).
/// @return JS object with: parameterCount, totalValues, strength, totalTuples,
///         estimatedTests, subModelCount, constraintCount, parameters[].
val wasmEstimateModel(val input) {
  try {
    RowStringBudget budget;
    auto opts_read = ReadGenerateOptions(RequireObjectArgument(std::move(input), "input"), budget);
    auto accepted = coverwise::model::AcceptOptions(std::move(opts_read), budget.charged);
    if (!accepted.ok()) {
      return MakeError(accepted.error());
    }
    auto stats = coverwise::core::EstimateModel(accepted->get());
    if (!stats.error.ok()) {
      return MakeError(stats.error);
    }
    return ModelStatsToJS(stats);
  } catch (const InputError& e) {
    return MakeError(e.error);
  } catch (const std::exception& e) {
    return MakeError(e.what());
  }
}

}  // namespace

EMSCRIPTEN_BINDINGS(coverwise) {
  function("generate", &wasmGenerate);
  function("analyzeCoverage", &wasmAnalyzeCoverage);
  function("extendTests", &wasmExtendTests);
  function("estimateModel", &wasmEstimateModel);
}

#endif  // __EMSCRIPTEN__
