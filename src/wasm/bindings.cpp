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
#include <optional>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/boundary.h"
#include "model/constraint_parser.h"
#include "model/error.h"
#include "model/parameter.h"
#include "validator/coverage_validator.h"

using namespace emscripten;

namespace {

// ---------------------------------------------------------------------------
// JS -> C++ conversion helpers
// ---------------------------------------------------------------------------

/// @brief Convert a JS value (string, number, or boolean) to a C++ string.
///
/// - string  → as-is
/// - number  → JS Number.prototype.toString() ("42", "3.14", "1e-7")
/// - boolean → "true" / "false"
///
/// Numbers are formatted through the JS runtime (String()), so the result is
/// byte-identical to the pure-JS adapter by construction. This keeps the WASM
/// and pure-JS surfaces in lockstep with zero duplicated numeric logic.
std::string JsValueToString(val item) {
  std::string type = item.typeOf().as<std::string>();
  if (type == "string") {
    return item.as<std::string>();
  }
  if (type == "boolean") {
    return item.as<bool>() ? "true" : "false";
  }
  // For numbers (and any other type), defer to JS String() conversion, which
  // implements the ECMAScript Number-to-String algorithm. String(42) === "42"
  // (no ".0"), String(-0) === "0", String(1e-7) === "1e-7".
  return val::global("String").call<std::string>("call", val::null(), item);
}

/// @brief Validate a JS number as a uint32 with public API validation.
///
/// Rejects non-numbers, non-finite values, fractional values, negatives, and
/// values beyond uint32 range. When @p allow_zero is false, zero is also rejected.
uint32_t ParseUint32Value(val raw, const char* field, bool allow_zero) {
  if (raw.typeOf().as<std::string>() != "number") {
    throw std::runtime_error(std::string("Invalid ") + field + ": must be a number");
  }
  double d = raw.as<double>();
  if (!std::isfinite(d) || std::floor(d) != d || d < 0.0 ||
      d > static_cast<double>(std::numeric_limits<uint32_t>::max())) {
    throw std::runtime_error(std::string("Invalid ") + field + ": must be a non-negative integer");
  }
  if (!allow_zero && d == 0.0) {
    throw std::runtime_error(std::string("Invalid ") + field + ": must be a positive integer");
  }
  return static_cast<uint32_t>(d);
}

/// @brief Parse a JS object field as a uint32 with public API validation.
uint32_t ParseUint32Option(val input, const char* field, bool allow_zero) {
  return ParseUint32Value(input[field], field, allow_zero);
}

std::optional<coverwise::model::BoundaryConfig> ParseBoundaryConfigForParam(val js_param);

/// @brief Parse a single JS parameter object into a C++ Parameter.
///
/// Handles three value formats:
///   - Simple string: "chrome"
///   - Object with value: { value: "ie6", invalid: true }
///   - Object with aliases: { value: "chromium", aliases: ["chrome", "edge"] }
///
/// When the parameter carries boundary fields (type/range/step), the value set
/// is expanded up front so the returned Parameter is the single source of truth
/// for both generation and rendering.
coverwise::model::Parameter ParseParameter(val js_param) {
  coverwise::model::Parameter param;

  // Validate shape BEFORE dereferencing. Under DISABLE_EXCEPTION_CATCHING a raw
  // emscripten type error would abort the module and poison the singleton, so we
  // throw a std::runtime_error (caught at the boundary → INVALID_INPUT) instead.
  val js_name = js_param["name"];
  if (js_name.typeOf().as<std::string>() != "string") {
    throw std::runtime_error("Parameter name must be a non-empty string");
  }
  param.name = js_name.as<std::string>();

  val js_values = js_param["values"];
  // Array.isArray guards against a string (indexable via .length, which would be
  // silently iterated char-by-char) or any non-array value.
  if (!val::global("Array").call<bool>("isArray", js_values)) {
    throw std::runtime_error("Parameter '" + param.name + "' must have at least one value");
  }
  uint32_t count = js_values["length"].as<uint32_t>();

  std::vector<bool> invalid_flags;
  std::vector<std::vector<std::string>> aliases;
  std::vector<std::string> eq_classes;
  bool has_invalid = false;
  bool has_aliases = false;
  bool has_classes = false;

  for (uint32_t i = 0; i < count; ++i) {
    val item = js_values[i];
    std::string item_type = item.typeOf().as<std::string>();
    if (item_type == "string" || item_type == "number" || item_type == "boolean") {
      // Scalar value — convert to string
      param.values.push_back(JsValueToString(item));
      invalid_flags.push_back(false);
      aliases.emplace_back();
      eq_classes.emplace_back();
    } else {
      // Object form: { value: "...", invalid?: bool, aliases?: [...], class?: "..." }
      param.values.push_back(JsValueToString(item["value"]));

      bool is_invalid = false;
      if (item.hasOwnProperty("invalid")) {
        is_invalid = item["invalid"].as<bool>();
      }
      invalid_flags.push_back(is_invalid);
      if (is_invalid) has_invalid = true;

      std::vector<std::string> value_aliases;
      if (item.hasOwnProperty("aliases")) {
        val js_aliases = item["aliases"];
        uint32_t alias_count = js_aliases["length"].as<uint32_t>();
        for (uint32_t a = 0; a < alias_count; ++a) {
          value_aliases.push_back(js_aliases[a].as<std::string>());
        }
        if (!value_aliases.empty()) has_aliases = true;
      }
      aliases.push_back(std::move(value_aliases));

      // Equivalence class (non-empty string).
      std::string eq_class;
      if (item.hasOwnProperty("class")) {
        val js_class = item["class"];
        if (js_class.typeOf().as<std::string>() == "string") {
          eq_class = js_class.as<std::string>();
        }
      }
      if (!eq_class.empty()) has_classes = true;
      eq_classes.push_back(std::move(eq_class));
    }
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

  // Apply boundary value expansion up front. ExpandBoundaryValues regenerates
  // the value set (dropping aliases/classes), so the returned Parameter alone
  // describes the value space used for both generation and rendering.
  auto boundary = ParseBoundaryConfigForParam(js_param);
  if (boundary) {
    param = coverwise::model::ExpandBoundaryValues(param, *boundary);
  }
  return param;
}

/// @brief Derive a boundary expansion config from a single JS parameter object.
///
/// A parameter participates when it carries a `type` ("integer" | "float") and a
/// 2-element numeric `range` ([min, max]). For the float type an optional `step`
/// (default 1.0) sets the spacing. Returns std::nullopt when the parameter does
/// not opt into boundary expansion. Mirrors the CLI's ParseBoundaryConfigs and
/// the pure-JS boundaryConfigFromParam canonical shape.
std::optional<coverwise::model::BoundaryConfig> ParseBoundaryConfigForParam(val js_param) {
  if (!js_param.hasOwnProperty("type") || !js_param.hasOwnProperty("range")) {
    return std::nullopt;
  }
  val js_type = js_param["type"];
  if (js_type.typeOf().as<std::string>() != "string") {
    return std::nullopt;
  }
  val js_range = js_param["range"];
  if (!val::global("Array").call<bool>("isArray", js_range) ||
      js_range["length"].as<uint32_t>() != 2) {
    return std::nullopt;
  }
  if (js_range[0].typeOf().as<std::string>() != "number" ||
      js_range[1].typeOf().as<std::string>() != "number") {
    return std::nullopt;
  }

  coverwise::model::BoundaryConfig config;
  config.min_value = js_range[0].as<double>();
  config.max_value = js_range[1].as<double>();

  std::string type = js_type.as<std::string>();
  if (type == "integer") {
    config.type = coverwise::model::BoundaryConfig::Type::kInteger;
    config.step = 1.0;
  } else if (type == "float") {
    config.type = coverwise::model::BoundaryConfig::Type::kFloat;
    if (js_param.hasOwnProperty("step") &&
        js_param["step"].typeOf().as<std::string>() == "number") {
      config.step = js_param["step"].as<double>();
    } else {
      config.step = 1.0;
    }
  } else {
    return std::nullopt;
  }
  return config;
}

/// @brief Parse a JS parameters array into a C++ vector.
std::vector<coverwise::model::Parameter> ParseParameters(val js_params) {
  if (!val::global("Array").call<bool>("isArray", js_params)) {
    throw std::runtime_error("Invalid parameters: must be an array.");
  }
  std::vector<coverwise::model::Parameter> params;
  uint32_t count = js_params["length"].as<uint32_t>();
  params.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    params.push_back(ParseParameter(js_params[i]));
  }
  // Semantic checks (duplicate names/values, empty values) shared with the
  // pure-JS surface and the CLI via the model layer.
  auto err = coverwise::model::ValidateParameters(params);
  if (!err.ok()) {
    throw std::runtime_error(err.message);
  }
  return params;
}

/// @brief Parse a JS test case object into a C++ TestCase using parameter definitions.
///
/// The JS test case is a map of param_name -> value_string. We resolve each
/// value to its index using find_value_index.
coverwise::model::TestCase ParseTestCase(val js_test,
                                         const std::vector<coverwise::model::Parameter>& params) {
  coverwise::model::TestCase tc;
  tc.values.resize(params.size(), coverwise::model::kUnassigned);

  // Build a map from JS object keys to their values.
  // Using Object.keys() avoids hasOwnProperty(const char*) which
  // can fail with non-ASCII (UTF-8) property names in Emscripten.
  val keys = val::global("Object").call<val>("keys", js_test);
  uint32_t key_count = keys["length"].as<uint32_t>();

  for (uint32_t k = 0; k < key_count; ++k) {
    std::string key = keys[k].as<std::string>();
    // Find matching parameter
    for (uint32_t i = 0; i < params.size(); ++i) {
      if (params[i].name == key) {
        std::string val_str = JsValueToString(js_test[key]);
        uint32_t idx = params[i].find_value_index(val_str);
        if (idx == UINT32_MAX) {
          throw std::runtime_error("Unknown value '" + val_str + "' for parameter '" +
                                   params[i].name + "'");
        }
        tc.values[i] = idx;
        break;
      }
    }
  }
  return tc;
}

/// @brief Parse a JS array of test case objects into C++ TestCase vector.
std::vector<coverwise::model::TestCase> ParseTestCases(
    val js_tests, const std::vector<coverwise::model::Parameter>& params) {
  if (!val::global("Array").call<bool>("isArray", js_tests)) {
    throw std::runtime_error("Invalid tests: must be an array.");
  }
  std::vector<coverwise::model::TestCase> tests;
  uint32_t count = js_tests["length"].as<uint32_t>();
  tests.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    tests.push_back(ParseTestCase(js_tests[i], params));
  }
  return tests;
}

/// @brief Parse weight configuration from JS object.
///
/// Expected format: { "os": { "win": 2.0, "mac": 1.5 }, "browser": { ... } }
coverwise::model::WeightConfig ParseWeights(val js_weights) {
  coverwise::model::WeightConfig weights;
  if (js_weights.isUndefined() || js_weights.isNull()) return weights;

  val keys = val::global("Object").call<val>("keys", js_weights);
  uint32_t key_count = keys["length"].as<uint32_t>();
  for (uint32_t i = 0; i < key_count; ++i) {
    std::string param_name = keys[i].as<std::string>();
    val param_weights = js_weights[param_name];
    val value_keys = val::global("Object").call<val>("keys", param_weights);
    uint32_t vk_count = value_keys["length"].as<uint32_t>();
    for (uint32_t j = 0; j < vk_count; ++j) {
      std::string value_name = value_keys[j].as<std::string>();
      double weight = param_weights[value_name].as<double>();
      weights.entries[param_name][value_name] = weight;
    }
  }
  return weights;
}

/// @brief Parse sub-models from JS array.
///
/// Expected format: [{ parameters: ["os", "browser"], strength: 3 }, ...]
std::vector<coverwise::model::SubModel> ParseSubModels(val js_sub_models) {
  std::vector<coverwise::model::SubModel> sub_models;
  if (js_sub_models.isUndefined() || js_sub_models.isNull()) return sub_models;

  uint32_t count = js_sub_models["length"].as<uint32_t>();
  sub_models.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    val js_sm = js_sub_models[i];
    coverwise::model::SubModel sm;
    val js_names = js_sm["parameters"];
    uint32_t name_count = js_names["length"].as<uint32_t>();
    for (uint32_t j = 0; j < name_count; ++j) {
      sm.parameter_names.push_back(js_names[j].as<std::string>());
    }
    if (js_sm.hasOwnProperty("strength")) {
      // Validate as a positive integer; a fractional or zero strength would
      // otherwise be silently truncated (e.g. 0 → no-op sub-model).
      sm.strength = ParseUint32Value(js_sm["strength"], "subModel strength", false);
    }
    sub_models.push_back(std::move(sm));
  }
  return sub_models;
}

/// @brief Parse the full GenerateOptions from a JS input object.
coverwise::model::GenerateOptions ParseGenerateOptions(val input) {
  coverwise::model::GenerateOptions opts;

  // Parameters (required)
  if (!input.hasOwnProperty("parameters")) {
    throw std::runtime_error("Missing required field 'parameters'");
  }
  opts.parameters = ParseParameters(input["parameters"]);

  // Strength (default 2)
  if (input.hasOwnProperty("strength")) {
    opts.strength = ParseUint32Option(input, "strength", false);
  }

  // Seed (default 0). Canonical domain: integer in [0, 2^32 - 1].
  if (input.hasOwnProperty("seed")) {
    // JS numbers are doubles; validate before casting to avoid UB on negative
    // values or silent wraparound on out-of-range values.
    double seed_d = input["seed"].as<double>();
    if (seed_d != std::floor(seed_d) || seed_d < 0.0 || seed_d > 4294967295.0) {
      throw std::runtime_error("Invalid seed: must be an integer in [0, 4294967295]");
    }
    opts.seed = static_cast<uint64_t>(seed_d);
  }

  // Max tests (default 0 = no limit)
  if (input.hasOwnProperty("maxTests")) {
    opts.max_tests = ParseUint32Option(input, "maxTests", true);
  }

  // Constraint expressions (strings)
  if (input.hasOwnProperty("constraints")) {
    val js_constraints = input["constraints"];
    uint32_t count = js_constraints["length"].as<uint32_t>();
    for (uint32_t i = 0; i < count; ++i) {
      opts.constraint_expressions.push_back(js_constraints[i].as<std::string>());
    }
  }

  // Seed tests (existing tests to build upon)
  if (input.hasOwnProperty("seeds")) {
    opts.seeds = ParseTestCases(input["seeds"], opts.parameters);
  }

  // Weights
  if (input.hasOwnProperty("weights")) {
    opts.weights = ParseWeights(input["weights"]);
  }

  // Sub-models
  if (input.hasOwnProperty("subModels")) {
    opts.sub_models = ParseSubModels(input["subModels"]);
  }

  // Boundary expansion is applied during ParseParameter, so opts.parameters
  // already holds the expanded value set; opts.boundary_configs stays empty to
  // avoid a second (no-op) expansion in core::Generate.

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
  val obj = val::object();
  for (uint32_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
    if (tc.values[i] != coverwise::model::kUnassigned) {
      obj.set(params[i].name, params[i].display_name(tc.values[i], rotation));
    }
  }
  return obj;
}

/// @brief Convert a vector of test cases to a JS array.
val TestCasesToJS(const std::vector<coverwise::model::TestCase>& tests,
                  const std::vector<coverwise::model::Parameter>& params) {
  val arr = val::array();
  for (uint32_t i = 0; i < tests.size(); ++i) {
    arr.call<void>("push", TestCaseToJS(tests[i], params, i));
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

    obj.set("reason", ut.reason);
    obj.set("display", val(ut.ToString()));

    arr.call<void>("push", obj);
  }
  return arr;
}

/// @brief Convert a GenerateResult to a JS object.
val GenerateResultToJS(const coverwise::model::GenerateResult& result,
                       const std::vector<coverwise::model::Parameter>& params, uint32_t strength) {
  val obj = val::object();

  // strength
  obj.set("strength", val(strength));

  // tests
  obj.set("tests", TestCasesToJS(result.tests, params));

  // negativeTests
  obj.set("negativeTests", TestCasesToJS(result.negative_tests, params));

  // coverage
  obj.set("coverage", result.coverage);

  // uncovered
  obj.set("uncovered", UncoveredToJS(result.uncovered));

  // stats
  val stats = val::object();
  stats.set("totalTuples", result.stats.total_tuples);
  stats.set("coveredTuples", result.stats.covered_tuples);
  stats.set("testCount", result.stats.test_count);
  obj.set("stats", stats);

  // suggestions
  val suggestions = val::array();
  for (uint32_t i = 0; i < result.suggestions.size(); ++i) {
    val s = val::object();
    s.set("description", result.suggestions[i].description);
    s.set("testCase", TestCaseToJS(result.suggestions[i].test_case, params, i));
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
    cc.set("totalClassTuples", result.class_coverage->total_class_tuples);
    cc.set("coveredClassTuples", result.class_coverage->covered_class_tuples);
    cc.set("classCoverageRatio", result.class_coverage->class_coverage_ratio);
    obj.set("classCoverage", cc);
  }

  return obj;
}

/// @brief Convert a CoverageReport to a JS object.
val CoverageReportToJS(const coverwise::validator::CoverageReport& report) {
  val obj = val::object();
  obj.set("totalTuples", report.total_tuples);
  obj.set("coveredTuples", report.covered_tuples);
  obj.set("coverageRatio", report.coverage_ratio);
  obj.set("uncovered", UncoveredToJS(report.uncovered));
  return obj;
}

/// @brief Convert ModelStats to a JS object.
val ModelStatsToJS(const coverwise::model::ModelStats& stats) {
  val obj = val::object();
  obj.set("parameterCount", stats.parameter_count);
  obj.set("totalValues", stats.total_values);
  obj.set("strength", stats.strength);
  obj.set("totalTuples", stats.total_tuples);
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
val MakeError(const std::string& message, int code = 3) {
  val obj = val::object();
  obj.set("error", true);
  obj.set("code", code);
  obj.set("message", val(message));
  return obj;
}

/// @brief Build a JS error object from a structured core Error.
val MakeError(const coverwise::model::Error& error) {
  std::string message = error.message;
  if (!error.detail.empty()) {
    message += ": " + error.detail;
  }
  return MakeError(message, static_cast<int>(error.code));
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
    auto opts = ParseGenerateOptions(input);
    auto result = coverwise::core::Generate(opts);

    // Core reports early-exit failures (e.g. constraint parse errors) via
    // result.error rather than throwing. Propagate the real code to JS.
    if (!result.error.ok()) {
      return MakeError(result.error);
    }

    // Annotate equivalence class coverage if any parameter has classes defined.
    coverwise::validator::AnnotateClassCoverage(result, opts.parameters, opts.strength);

    return GenerateResultToJS(result, opts.parameters, opts.strength);
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
    uint32_t strength = ParseUint32Value(js_strength, "strength", false);
    auto params = ParseParameters(js_params);
    auto tests = ParseTestCases(js_tests, params);

    // Parse optional constraint expressions. Mirrors ParseGenerateOptions.
    std::vector<coverwise::model::Constraint> constraints;
    if (!js_constraints.isUndefined() && !js_constraints.isNull()) {
      uint32_t count = js_constraints["length"].as<uint32_t>();
      for (uint32_t i = 0; i < count; ++i) {
        std::string expr = js_constraints[i].as<std::string>();
        auto parse_result = coverwise::model::ParseConstraint(expr, params);
        if (!parse_result.error.ok()) {
          return MakeError(parse_result.error);
        }
        constraints.push_back(std::move(parse_result.constraint));
      }
    }

    auto report = coverwise::validator::ValidateCoverage(params, tests, strength, constraints);
    return CoverageReportToJS(report);
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
    auto opts = ParseGenerateOptions(input);
    auto existing = ParseTestCases(js_existing, opts.parameters);
    auto result = coverwise::core::Extend(existing, opts);
    if (!result.error.ok()) {
      return MakeError(result.error);
    }
    return GenerateResultToJS(result, opts.parameters, opts.strength);
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
    auto opts = ParseGenerateOptions(input);
    auto stats = coverwise::core::EstimateModel(opts);
    return ModelStatsToJS(stats);
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
