/// @file bindings.cpp
/// @brief Emscripten embind bindings for coverwise WASM module.
///
/// Design: JSON parsing is done on the JS side. The WASM module receives and
/// returns JavaScript objects (emscripten::val). The JS wrapper is thin — it
/// just calls the WASM function with a JS object and gets a JS object back.

#ifdef __EMSCRIPTEN__

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/constraint_parser.h"
#include "validator/coverage_validator.h"

using namespace emscripten;

namespace {

// ---------------------------------------------------------------------------
// JS -> C++ conversion helpers
// ---------------------------------------------------------------------------

/// @brief Parse a single JS parameter object into a C++ Parameter.
///
/// Handles three value formats:
///   - Simple string: "chrome"
///   - Object with value: { value: "ie6", invalid: true }
///   - Object with aliases: { value: "chromium", aliases: ["chrome", "edge"] }
coverwise::model::Parameter ParseParameter(val js_param) {
  coverwise::model::Parameter param;
  param.name = js_param["name"].as<std::string>();

  val js_values = js_param["values"];
  uint32_t count = js_values["length"].as<uint32_t>();

  std::vector<bool> invalid_flags;
  std::vector<std::vector<std::string>> aliases;
  bool has_invalid = false;
  bool has_aliases = false;

  for (uint32_t i = 0; i < count; ++i) {
    val item = js_values[i];
    if (item.typeOf().as<std::string>() == "string") {
      // Simple string value
      param.values.push_back(item.as<std::string>());
      invalid_flags.push_back(false);
      aliases.emplace_back();
    } else {
      // Object form: { value: "...", invalid?: bool, aliases?: [...] }
      param.values.push_back(item["value"].as<std::string>());

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
    }
  }

  if (has_invalid) {
    param.set_invalid(std::move(invalid_flags));
  }
  if (has_aliases) {
    param.set_aliases(std::move(aliases));
  }
  return param;
}

/// @brief Parse a JS parameters array into a C++ vector.
std::vector<coverwise::model::Parameter> ParseParameters(val js_params) {
  std::vector<coverwise::model::Parameter> params;
  uint32_t count = js_params["length"].as<uint32_t>();
  params.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    params.push_back(ParseParameter(js_params[i]));
  }
  return params;
}

/// @brief Parse a JS test case object into a C++ TestCase using parameter definitions.
///
/// The JS test case is a map of param_name -> value_string. We resolve each
/// value to its index using find_value_index.
coverwise::model::TestCase ParseTestCase(
    val js_test, const std::vector<coverwise::model::Parameter>& params) {
  coverwise::model::TestCase tc;
  tc.values.resize(params.size(), coverwise::model::kUnassigned);
  for (uint32_t i = 0; i < params.size(); ++i) {
    if (js_test.hasOwnProperty(params[i].name.c_str())) {
      std::string val_str = js_test[params[i].name].as<std::string>();
      tc.values[i] = params[i].find_value_index(val_str);
    }
  }
  return tc;
}

/// @brief Parse a JS array of test case objects into C++ TestCase vector.
std::vector<coverwise::model::TestCase> ParseTestCases(
    val js_tests, const std::vector<coverwise::model::Parameter>& params) {
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
coverwise::core::WeightConfig ParseWeights(val js_weights) {
  coverwise::core::WeightConfig weights;
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
std::vector<coverwise::core::SubModel> ParseSubModels(val js_sub_models) {
  std::vector<coverwise::core::SubModel> sub_models;
  if (js_sub_models.isUndefined() || js_sub_models.isNull()) return sub_models;

  uint32_t count = js_sub_models["length"].as<uint32_t>();
  sub_models.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    val js_sm = js_sub_models[i];
    coverwise::core::SubModel sm;
    val js_names = js_sm["parameters"];
    uint32_t name_count = js_names["length"].as<uint32_t>();
    for (uint32_t j = 0; j < name_count; ++j) {
      sm.parameter_names.push_back(js_names[j].as<std::string>());
    }
    if (js_sm.hasOwnProperty("strength")) {
      sm.strength = js_sm["strength"].as<uint32_t>();
    }
    sub_models.push_back(std::move(sm));
  }
  return sub_models;
}

/// @brief Parse the full GenerateOptions from a JS input object.
coverwise::core::GenerateOptions ParseGenerateOptions(val input) {
  coverwise::core::GenerateOptions opts;

  // Parameters (required)
  opts.parameters = ParseParameters(input["parameters"]);

  // Strength (default 2)
  if (input.hasOwnProperty("strength")) {
    opts.strength = input["strength"].as<uint32_t>();
  }

  // Seed (default 0)
  if (input.hasOwnProperty("seed")) {
    // JS numbers are doubles; cast to uint64_t.
    opts.seed = static_cast<uint64_t>(input["seed"].as<double>());
  }

  // Max tests (default 0 = no limit)
  if (input.hasOwnProperty("maxTests")) {
    opts.max_tests = input["maxTests"].as<uint32_t>();
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

  return opts;
}

// ---------------------------------------------------------------------------
// C++ -> JS conversion helpers
// ---------------------------------------------------------------------------

/// @brief Convert a test case to a JS object with param_name -> value_string.
///
/// Uses display_name() for alias rotation.
val TestCaseToJS(const coverwise::model::TestCase& tc,
                 const std::vector<coverwise::model::Parameter>& params,
                 uint32_t rotation) {
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
                       const std::vector<coverwise::model::Parameter>& params) {
  val obj = val::object();

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
val ModelStatsToJS(const coverwise::core::ModelStats& stats) {
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

/// @brief Create a JS error object.
val MakeError(const std::string& message, int code = 3) {
  val obj = val::object();
  obj.set("error", true);
  obj.set("code", code);
  obj.set("message", val(message));
  return obj;
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
    return GenerateResultToJS(result, opts.parameters);
  } catch (const std::exception& e) {
    return MakeError(e.what());
  }
}

/// @brief Analyze coverage of existing tests against parameters.
///
/// @param js_params JS array of parameter definitions.
/// @param js_tests JS array of test case objects (param_name -> value_string).
/// @param strength Interaction strength (2 = pairwise).
/// @return JS object with: totalTuples, coveredTuples, coverageRatio, uncovered.
val wasmAnalyzeCoverage(val js_params, val js_tests, uint32_t strength) {
  try {
    auto params = ParseParameters(js_params);
    auto tests = ParseTestCases(js_tests, params);
    auto report = coverwise::validator::ValidateCoverage(params, tests, strength);
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
    return GenerateResultToJS(result, opts.parameters);
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
