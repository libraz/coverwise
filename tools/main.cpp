/// @file main.cpp
/// @brief coverwise CLI — combinatorial test design tool.
///
/// Usage:
///   coverwise generate input.json > tests.json
///   coverwise analyze --params params.json --tests tests.json
///   coverwise extend --existing tests.json input.json > additional.json
///
/// Exit codes:
///   0 = OK (coverage 100%)
///   1 = Constraint error
///   2 = Insufficient coverage (max_tests limited)
///   3 = Invalid input

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/coverage_validator.h"

namespace {

constexpr int kExitOk = 0;
// Exit code 1 reserved for constraint error support.
constexpr int kExitInsufficientCoverage = 2;
constexpr int kExitInvalidInput = 3;

// ---------------------------------------------------------------------------
// Minimal JSON value representation — just enough for coverwise I/O.
// ---------------------------------------------------------------------------

enum class JsonType { kNull, kBool, kNumber, kString, kArray, kObject };

struct JsonValue {
  JsonType type = JsonType::kNull;
  std::string string_val;
  double number_val = 0.0;
  bool bool_val = false;
  std::vector<JsonValue> array_val;
  /// Parallel vectors for object keys and values (preserves insertion order).
  std::vector<std::string> object_keys;
  std::vector<JsonValue> object_vals;

  /// @brief Access an object member by key. Returns a null JsonValue if missing.
  const JsonValue& operator[](const std::string& key) const {
    for (size_t i = 0; i < object_keys.size(); ++i) {
      if (object_keys[i] == key) return object_vals[i];
    }
    static const JsonValue kNull;
    return kNull;
  }

  /// @brief Access an array element by index.
  const JsonValue& operator[](size_t index) const { return array_val[index]; }

  bool IsNull() const { return type == JsonType::kNull; }
  size_t Size() const { return type == JsonType::kArray ? array_val.size() : 0; }
};

// ---------------------------------------------------------------------------
// Minimal recursive-descent JSON parser.
// ---------------------------------------------------------------------------

class JsonParser {
 public:
  explicit JsonParser(const std::string& input) : input_(input), pos_(0) {}

  /// @brief Parse the input and return a JsonValue. Sets error on failure.
  JsonValue Parse() {
    SkipWhitespace();
    auto val = ParseValue();
    if (!error_.empty()) return {};
    SkipWhitespace();
    return val;
  }

  const std::string& error() const { return error_; }

 private:
  void SkipWhitespace() {
    while (pos_ < input_.size() && (input_[pos_] == ' ' || input_[pos_] == '\t' ||
                                    input_[pos_] == '\n' || input_[pos_] == '\r')) {
      ++pos_;
    }
  }

  char Peek() const { return pos_ < input_.size() ? input_[pos_] : '\0'; }

  char Next() { return pos_ < input_.size() ? input_[pos_++] : '\0'; }

  bool Expect(char c) {
    SkipWhitespace();
    if (Peek() == c) {
      ++pos_;
      return true;
    }
    error_ = std::string("expected '") + c + "' at position " + std::to_string(pos_);
    return false;
  }

  JsonValue ParseValue() {
    SkipWhitespace();
    char c = Peek();
    if (c == '"') return ParseString();
    if (c == '{') return ParseObject();
    if (c == '[') return ParseArray();
    if (c == 't' || c == 'f') return ParseBool();
    if (c == 'n') return ParseNull();
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
    error_ = std::string("unexpected character '") + c + "' at position " + std::to_string(pos_);
    return {};
  }

  JsonValue ParseString() {
    if (!Expect('"')) return {};
    JsonValue val;
    val.type = JsonType::kString;
    while (pos_ < input_.size() && input_[pos_] != '"') {
      if (input_[pos_] == '\\') {
        ++pos_;
        if (pos_ >= input_.size()) {
          error_ = "unterminated string escape";
          return {};
        }
        char esc = input_[pos_++];
        switch (esc) {
          case '"':
            val.string_val += '"';
            break;
          case '\\':
            val.string_val += '\\';
            break;
          case '/':
            val.string_val += '/';
            break;
          case 'n':
            val.string_val += '\n';
            break;
          case 't':
            val.string_val += '\t';
            break;
          case 'r':
            val.string_val += '\r';
            break;
          default:
            val.string_val += esc;
            break;
        }
      } else {
        val.string_val += input_[pos_++];
      }
    }
    if (pos_ >= input_.size()) {
      error_ = "unterminated string";
      return {};
    }
    ++pos_;  // skip closing "
    return val;
  }

  JsonValue ParseNumber() {
    size_t start = pos_;
    if (Peek() == '-') ++pos_;
    while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
    }
    if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
    }
    JsonValue val;
    val.type = JsonType::kNumber;
    val.number_val = std::stod(input_.substr(start, pos_ - start));
    return val;
  }

  JsonValue ParseBool() {
    JsonValue val;
    val.type = JsonType::kBool;
    if (input_.substr(pos_, 4) == "true") {
      val.bool_val = true;
      pos_ += 4;
    } else if (input_.substr(pos_, 5) == "false") {
      val.bool_val = false;
      pos_ += 5;
    } else {
      error_ = "invalid boolean at position " + std::to_string(pos_);
    }
    return val;
  }

  JsonValue ParseNull() {
    if (input_.substr(pos_, 4) == "null") {
      pos_ += 4;
      return {};
    }
    error_ = "invalid null at position " + std::to_string(pos_);
    return {};
  }

  JsonValue ParseArray() {
    if (!Expect('[')) return {};
    JsonValue val;
    val.type = JsonType::kArray;
    SkipWhitespace();
    if (Peek() == ']') {
      ++pos_;
      return val;
    }
    while (true) {
      val.array_val.push_back(ParseValue());
      if (!error_.empty()) return {};
      SkipWhitespace();
      if (Peek() == ',') {
        ++pos_;
        continue;
      }
      break;
    }
    if (!Expect(']')) return {};
    return val;
  }

  JsonValue ParseObject() {
    if (!Expect('{')) return {};
    JsonValue val;
    val.type = JsonType::kObject;
    SkipWhitespace();
    if (Peek() == '}') {
      ++pos_;
      return val;
    }
    while (true) {
      SkipWhitespace();
      auto key = ParseString();
      if (!error_.empty()) return {};
      if (!Expect(':')) return {};
      auto value = ParseValue();
      if (!error_.empty()) return {};
      val.object_keys.push_back(key.string_val);
      val.object_vals.push_back(std::move(value));
      SkipWhitespace();
      if (Peek() == ',') {
        ++pos_;
        continue;
      }
      break;
    }
    if (!Expect('}')) return {};
    return val;
  }

  std::string input_;
  size_t pos_;
  std::string error_;
};

// ---------------------------------------------------------------------------
// JSON writer — produces compact JSON on stdout.
// ---------------------------------------------------------------------------

class JsonWriter {
 public:
  explicit JsonWriter(std::ostream& out) : out_(out) {}

  void WriteNull() { out_ << "null"; }

  void WriteBool(bool v) { out_ << (v ? "true" : "false"); }

  void WriteNumber(double v) {
    // Integer values print without decimal point.
    if (v == static_cast<int64_t>(v) && v >= -1e15 && v <= 1e15) {
      out_ << static_cast<int64_t>(v);
    } else {
      out_ << std::setprecision(6) << v;
    }
  }

  void WriteString(const std::string& s) {
    out_ << '"';
    for (char c : s) {
      switch (c) {
        case '"':
          out_ << "\\\"";
          break;
        case '\\':
          out_ << "\\\\";
          break;
        case '\n':
          out_ << "\\n";
          break;
        case '\r':
          out_ << "\\r";
          break;
        case '\t':
          out_ << "\\t";
          break;
        default:
          out_ << c;
          break;
      }
    }
    out_ << '"';
  }

  /// @brief Begin a JSON array. Must be paired with EndArray().
  void BeginArray() {
    out_ << '[';
    stack_.push_back({true, true});
  }

  void EndArray() {
    out_ << ']';
    stack_.pop_back();
  }

  /// @brief Begin a JSON object. Must be paired with EndObject().
  void BeginObject() {
    out_ << '{';
    stack_.push_back({false, true});
  }

  void EndObject() {
    out_ << '}';
    stack_.pop_back();
  }

  /// @brief Write a comma separator if not the first element.
  void Sep() {
    if (!stack_.empty()) {
      if (stack_.back().first_element) {
        stack_.back().first_element = false;
      } else {
        out_ << ',';
      }
    }
  }

  /// @brief Write a key in an object context.
  void Key(const std::string& k) {
    Sep();
    WriteString(k);
    out_ << ':';
  }

 private:
  struct StackEntry {
    bool is_array;
    bool first_element;
  };
  std::ostream& out_;
  std::vector<StackEntry> stack_;
};

// ---------------------------------------------------------------------------
// File reading utility.
// ---------------------------------------------------------------------------

/// @brief Read entire file contents into a string. Returns empty on failure.
std::string ReadFile(const std::string& path) {
  std::ifstream file(path);
  if (!file.is_open()) return {};
  std::ostringstream ss;
  ss << file.rdbuf();
  return ss.str();
}

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
    bool has_any_invalid = false;
    bool has_any_aliases = false;
    std::vector<bool> invalid_flags;
    std::vector<std::vector<std::string>> aliases_list;
    for (size_t j = 0; j < values_val.array_val.size(); ++j) {
      const auto& v = values_val.array_val[j];
      if (v.type == JsonType::kString) {
        param.values.push_back(v.string_val);
        invalid_flags.push_back(false);
        aliases_list.push_back({});
      } else if (v.type == JsonType::kNumber) {
        // Convert number to string representation.
        if (v.number_val == static_cast<int64_t>(v.number_val)) {
          param.values.push_back(std::to_string(static_cast<int64_t>(v.number_val)));
        } else {
          std::ostringstream oss;
          oss << v.number_val;
          param.values.push_back(oss.str());
        }
        invalid_flags.push_back(false);
        aliases_list.push_back({});
      } else if (v.type == JsonType::kBool) {
        param.values.push_back(v.bool_val ? "true" : "false");
        invalid_flags.push_back(false);
        aliases_list.push_back({});
      } else if (v.type == JsonType::kObject) {
        // Object form: {"value": "...", "invalid": true, "aliases": ["..."]}
        const auto& val_field = v["value"];
        std::string val_str;
        if (val_field.type == JsonType::kString) {
          val_str = val_field.string_val;
        } else if (val_field.type == JsonType::kNumber) {
          if (val_field.number_val == static_cast<int64_t>(val_field.number_val)) {
            val_str = std::to_string(static_cast<int64_t>(val_field.number_val));
          } else {
            std::ostringstream oss;
            oss << val_field.number_val;
            val_str = oss.str();
          }
        } else if (val_field.type == JsonType::kBool) {
          val_str = val_field.bool_val ? "true" : "false";
        } else {
          error = "parameter '" + param.name + "' value " + std::to_string(j) +
                  " object missing 'value' field";
          return false;
        }
        param.values.push_back(val_str);
        const auto& inv_field = v["invalid"];
        bool is_invalid = (inv_field.type == JsonType::kBool && inv_field.bool_val);
        invalid_flags.push_back(is_invalid);
        if (is_invalid) has_any_invalid = true;

        // Parse aliases.
        std::vector<std::string> val_aliases;
        const auto& aliases_field = v["aliases"];
        if (!aliases_field.IsNull() && aliases_field.type == JsonType::kArray) {
          for (size_t k = 0; k < aliases_field.array_val.size(); ++k) {
            const auto& a = aliases_field.array_val[k];
            if (a.type == JsonType::kString) {
              val_aliases.push_back(a.string_val);
            } else {
              error = "parameter '" + param.name + "' value " + std::to_string(j) + " alias " +
                      std::to_string(k) + " must be a string";
              return false;
            }
          }
          if (!val_aliases.empty()) has_any_aliases = true;
        }
        aliases_list.push_back(std::move(val_aliases));
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
    if (param.values.empty()) {
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

/// @brief Parse test cases from a JSON array of objects with string values.
/// Each test object maps parameter names to value strings.
/// Returns value indices matching the parameter definitions.
bool ParseTests(const JsonValue& json, const std::vector<coverwise::model::Parameter>& params,
                std::vector<coverwise::model::TestCase>& tests, std::string& error) {
  if (json.type != JsonType::kArray) {
    error = "tests must be a JSON array";
    return false;
  }
  for (size_t i = 0; i < json.array_val.size(); ++i) {
    const auto& t = json.array_val[i];
    if (t.type != JsonType::kObject) {
      error = "test " + std::to_string(i) + " must be an object";
      return false;
    }
    coverwise::model::TestCase tc;
    tc.values.resize(params.size(), UINT32_MAX);

    for (size_t pi = 0; pi < params.size(); ++pi) {
      const auto& val = t[params[pi].name];
      if (val.IsNull()) {
        error = "test " + std::to_string(i) + " missing parameter '" + params[pi].name + "'";
        return false;
      }
      // Convert value to string for lookup.
      std::string val_str;
      if (val.type == JsonType::kString) {
        val_str = val.string_val;
      } else if (val.type == JsonType::kNumber) {
        if (val.number_val == static_cast<int64_t>(val.number_val)) {
          val_str = std::to_string(static_cast<int64_t>(val.number_val));
        } else {
          std::ostringstream oss;
          oss << val.number_val;
          val_str = oss.str();
        }
      } else if (val.type == JsonType::kBool) {
        val_str = val.bool_val ? "true" : "false";
      } else {
        error = "test " + std::to_string(i) + " parameter '" + params[pi].name +
                "' has non-scalar value";
        return false;
      }

      // Find the value index (checking primary values and aliases).
      uint32_t val_idx = params[pi].find_value_index(val_str);
      if (val_idx == UINT32_MAX) {
        error = "test " + std::to_string(i) + " parameter '" + params[pi].name +
                "' has unknown value '" + val_str + "'";
        return false;
      }
      tc.values[pi] = val_idx;
    }
    tests.push_back(std::move(tc));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Write coverwise output as JSON.
// ---------------------------------------------------------------------------

void WriteGenerateResult(const coverwise::model::GenerateResult& result,
                         const std::vector<coverwise::model::Parameter>& params,
                         uint32_t strength) {
  JsonWriter w(std::cout);
  w.BeginObject();

  // tests
  w.Key("tests");
  w.BeginArray();
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    const auto& tc = result.tests[ti];
    w.Sep();
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
      w.Key(params[i].name);
      w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
    }
    w.EndObject();
  }
  w.EndArray();

  // negativeTests
  if (!result.negative_tests.empty()) {
    w.Key("negativeTests");
    w.BeginArray();
    for (size_t ti = 0; ti < result.negative_tests.size(); ++ti) {
      const auto& tc = result.negative_tests[ti];
      w.Sep();
      w.BeginObject();
      for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
        w.Key(params[i].name);
        w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
      }
      w.EndObject();
    }
    w.EndArray();
  }

  // coverage
  w.Key("coverage");
  w.WriteNumber(result.coverage);

  // uncovered
  w.Key("uncovered");
  w.BeginArray();
  for (const auto& u : result.uncovered) {
    w.Sep();
    w.BeginObject();
    w.Key("tuple");
    w.BeginArray();
    for (const auto& s : u.tuple) {
      w.Sep();
      w.WriteString(s);
    }
    w.EndArray();
    w.Key("params");
    w.BeginArray();
    for (const auto& p : u.params) {
      w.Sep();
      w.WriteString(p);
    }
    w.EndArray();
    w.Key("reason");
    w.WriteString(u.reason);
    w.EndObject();
  }
  w.EndArray();

  // stats
  w.Key("stats");
  w.BeginObject();
  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(result.stats.total_tuples));
  w.Key("coveredTuples");
  w.WriteNumber(static_cast<double>(result.stats.covered_tuples));
  w.Key("testCount");
  w.WriteNumber(static_cast<double>(result.stats.test_count));
  w.EndObject();

  // suggestions
  w.Key("suggestions");
  w.BeginArray();
  for (const auto& s : result.suggestions) {
    w.Sep();
    w.WriteString(s.description);
  }
  w.EndArray();

  // warnings
  w.Key("warnings");
  w.BeginArray();
  for (const auto& warn : result.warnings) {
    w.Sep();
    w.WriteString(warn);
  }
  w.EndArray();

  // strength
  w.Key("strength");
  w.WriteNumber(static_cast<double>(strength));

  w.EndObject();
  std::cout << '\n';
}

void WriteCoverageReport(const coverwise::validator::CoverageReport& report) {
  JsonWriter w(std::cout);
  w.BeginObject();

  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(report.total_tuples));

  w.Key("coveredTuples");
  w.WriteNumber(static_cast<double>(report.covered_tuples));

  w.Key("coverageRatio");
  // Write with 3 decimal places for ratio.
  std::cout << std::fixed << std::setprecision(3) << report.coverage_ratio;
  // Reset stream state after manual write.
  std::cout << std::defaultfloat;

  w.Key("uncovered");
  w.BeginArray();
  for (const auto& u : report.uncovered) {
    w.Sep();
    w.BeginObject();
    w.Key("tuple");
    w.BeginArray();
    for (const auto& s : u.tuple) {
      w.Sep();
      w.WriteString(s);
    }
    w.EndArray();
    w.Key("params");
    w.BeginArray();
    for (const auto& p : u.params) {
      w.Sep();
      w.WriteString(p);
    }
    w.EndArray();
    w.Key("reason");
    w.WriteString(u.reason);
    w.EndObject();
  }
  w.EndArray();

  w.EndObject();
  std::cout << '\n';
}

// ---------------------------------------------------------------------------
// Command implementations.
// ---------------------------------------------------------------------------

int RunGenerate(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: coverwise generate <input.json>\n";
    return kExitInvalidInput;
  }

  std::string content = ReadFile(argv[2]);
  if (content.empty()) {
    std::cerr << "error: cannot read file '" << argv[2] << "'\n";
    return kExitInvalidInput;
  }

  JsonParser parser(content);
  auto json = parser.Parse();
  if (!parser.error().empty()) {
    std::cerr << "error: invalid JSON: " << parser.error() << "\n";
    return kExitInvalidInput;
  }
  if (json.type != JsonType::kObject) {
    std::cerr << "error: input must be a JSON object\n";
    return kExitInvalidInput;
  }

  // Parse parameters.
  std::string error;
  coverwise::core::GenerateOptions options;
  if (!ParseParameters(json["parameters"], options.parameters, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse optional fields.
  const auto& strength_val = json["strength"];
  if (!strength_val.IsNull() && strength_val.type == JsonType::kNumber) {
    options.strength = static_cast<uint32_t>(strength_val.number_val);
  }
  const auto& seed_val = json["seed"];
  if (!seed_val.IsNull() && seed_val.type == JsonType::kNumber) {
    options.seed = static_cast<uint64_t>(seed_val.number_val);
  }
  const auto& max_tests_val = json["maxTests"];
  if (!max_tests_val.IsNull() && max_tests_val.type == JsonType::kNumber) {
    options.max_tests = static_cast<uint32_t>(max_tests_val.number_val);
  }

  // Parse constraints (array of strings).
  const auto& constraints_val = json["constraints"];
  if (!constraints_val.IsNull() && constraints_val.type == JsonType::kArray) {
    for (size_t i = 0; i < constraints_val.array_val.size(); ++i) {
      const auto& c = constraints_val.array_val[i];
      if (c.type != JsonType::kString) {
        std::cerr << "error: constraint " << i << " must be a string\n";
        return kExitInvalidInput;
      }
      options.constraint_expressions.push_back(c.string_val);
    }
  }

  // Parse weights: {"param_name": {"value_name": weight, ...}, ...}
  const auto& weights_val = json["weights"];
  if (!weights_val.IsNull() && weights_val.type == JsonType::kObject) {
    for (size_t i = 0; i < weights_val.object_keys.size(); ++i) {
      const auto& param_name = weights_val.object_keys[i];
      const auto& param_weights = weights_val.object_vals[i];
      if (param_weights.type != JsonType::kObject) {
        std::cerr << "error: weights for '" << param_name << "' must be an object\n";
        return kExitInvalidInput;
      }
      for (size_t j = 0; j < param_weights.object_keys.size(); ++j) {
        const auto& value_name = param_weights.object_keys[j];
        const auto& weight_val = param_weights.object_vals[j];
        if (weight_val.type != JsonType::kNumber) {
          std::cerr << "error: weight for '" << param_name << "." << value_name
                    << "' must be a number\n";
          return kExitInvalidInput;
        }
        options.weights.entries[param_name][value_name] = weight_val.number_val;
      }
    }
  }

  // Generate.
  auto result = coverwise::core::Generate(options);

  // Determine exit code.
  int exit_code = kExitOk;
  if (result.coverage < 1.0 && options.max_tests > 0) {
    exit_code = kExitInsufficientCoverage;
  }

  WriteGenerateResult(result, options.parameters, options.strength);
  return exit_code;
}

int RunAnalyze(int argc, char* argv[]) {
  // Parse --params and --tests flags.
  std::string params_path;
  std::string tests_path;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--params" && i + 1 < argc) {
      params_path = argv[++i];
    } else if (arg == "--tests" && i + 1 < argc) {
      tests_path = argv[++i];
    }
  }

  if (params_path.empty() || tests_path.empty()) {
    std::cerr << "Usage: coverwise analyze --params <params.json> --tests <tests.json>\n";
    return kExitInvalidInput;
  }

  // Read and parse params.
  std::string params_content = ReadFile(params_path);
  if (params_content.empty()) {
    std::cerr << "error: cannot read file '" << params_path << "'\n";
    return kExitInvalidInput;
  }
  JsonParser params_parser(params_content);
  auto params_json = params_parser.Parse();
  if (!params_parser.error().empty()) {
    std::cerr << "error: invalid params JSON: " << params_parser.error() << "\n";
    return kExitInvalidInput;
  }

  std::string error;
  std::vector<coverwise::model::Parameter> params;
  if (!ParseParameters(params_json, params, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Read and parse tests.
  std::string tests_content = ReadFile(tests_path);
  if (tests_content.empty()) {
    std::cerr << "error: cannot read file '" << tests_path << "'\n";
    return kExitInvalidInput;
  }
  JsonParser tests_parser(tests_content);
  auto tests_json = tests_parser.Parse();
  if (!tests_parser.error().empty()) {
    std::cerr << "error: invalid tests JSON: " << tests_parser.error() << "\n";
    return kExitInvalidInput;
  }

  std::vector<coverwise::model::TestCase> tests;
  if (!ParseTests(tests_json, params, tests, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Default to pairwise (strength=2). Could add --strength flag later.
  uint32_t strength = 2;
  auto report = coverwise::validator::ValidateCoverage(params, tests, strength);

  WriteCoverageReport(report);

  if (report.coverage_ratio < 1.0) {
    return kExitInsufficientCoverage;
  }
  return kExitOk;
}

int RunExtend(int argc, char* argv[]) {
  // Parse --existing flag and input file.
  std::string existing_path;
  std::string input_path;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--existing" && i + 1 < argc) {
      existing_path = argv[++i];
    } else if (input_path.empty() && arg[0] != '-') {
      input_path = arg;
    }
  }

  if (existing_path.empty() || input_path.empty()) {
    std::cerr << "Usage: coverwise extend --existing <tests.json> <input.json>\n";
    return kExitInvalidInput;
  }

  // Read and parse input.
  std::string input_content = ReadFile(input_path);
  if (input_content.empty()) {
    std::cerr << "error: cannot read file '" << input_path << "'\n";
    return kExitInvalidInput;
  }
  JsonParser input_parser(input_content);
  auto input_json = input_parser.Parse();
  if (!input_parser.error().empty()) {
    std::cerr << "error: invalid input JSON: " << input_parser.error() << "\n";
    return kExitInvalidInput;
  }
  if (input_json.type != JsonType::kObject) {
    std::cerr << "error: input must be a JSON object\n";
    return kExitInvalidInput;
  }

  std::string error;
  coverwise::core::GenerateOptions options;
  if (!ParseParameters(input_json["parameters"], options.parameters, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  const auto& strength_val = input_json["strength"];
  if (!strength_val.IsNull() && strength_val.type == JsonType::kNumber) {
    options.strength = static_cast<uint32_t>(strength_val.number_val);
  }
  const auto& seed_val = input_json["seed"];
  if (!seed_val.IsNull() && seed_val.type == JsonType::kNumber) {
    options.seed = static_cast<uint64_t>(seed_val.number_val);
  }
  const auto& max_tests_val = input_json["maxTests"];
  if (!max_tests_val.IsNull() && max_tests_val.type == JsonType::kNumber) {
    options.max_tests = static_cast<uint32_t>(max_tests_val.number_val);
  }

  // Parse constraints (array of strings).
  const auto& constraints_val = input_json["constraints"];
  if (!constraints_val.IsNull() && constraints_val.type == JsonType::kArray) {
    for (size_t i = 0; i < constraints_val.array_val.size(); ++i) {
      const auto& c = constraints_val.array_val[i];
      if (c.type != JsonType::kString) {
        std::cerr << "error: constraint " << i << " must be a string\n";
        return kExitInvalidInput;
      }
      options.constraint_expressions.push_back(c.string_val);
    }
  }

  // Read and parse existing tests.
  std::string existing_content = ReadFile(existing_path);
  if (existing_content.empty()) {
    std::cerr << "error: cannot read file '" << existing_path << "'\n";
    return kExitInvalidInput;
  }
  JsonParser existing_parser(existing_content);
  auto existing_json = existing_parser.Parse();
  if (!existing_parser.error().empty()) {
    std::cerr << "error: invalid existing tests JSON: " << existing_parser.error() << "\n";
    return kExitInvalidInput;
  }

  std::vector<coverwise::model::TestCase> existing;
  if (!ParseTests(existing_json, options.parameters, existing, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  auto result = coverwise::core::Extend(existing, options);

  int exit_code = kExitOk;
  if (result.coverage < 1.0 && options.max_tests > 0) {
    exit_code = kExitInsufficientCoverage;
  }

  WriteGenerateResult(result, options.parameters, options.strength);
  return exit_code;
}

int RunStats(int argc, char* argv[]) {
  if (argc < 3) {
    std::cerr << "Usage: coverwise stats <input.json>\n";
    return kExitInvalidInput;
  }

  std::string content = ReadFile(argv[2]);
  if (content.empty()) {
    std::cerr << "error: cannot read file '" << argv[2] << "'\n";
    return kExitInvalidInput;
  }

  JsonParser parser(content);
  auto json = parser.Parse();
  if (!parser.error().empty()) {
    std::cerr << "error: invalid JSON: " << parser.error() << "\n";
    return kExitInvalidInput;
  }
  if (json.type != JsonType::kObject) {
    std::cerr << "error: input must be a JSON object\n";
    return kExitInvalidInput;
  }

  std::string error;
  coverwise::core::GenerateOptions options;
  if (!ParseParameters(json["parameters"], options.parameters, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  const auto& strength_val = json["strength"];
  if (!strength_val.IsNull() && strength_val.type == JsonType::kNumber) {
    options.strength = static_cast<uint32_t>(strength_val.number_val);
  }

  const auto& constraints_val = json["constraints"];
  if (!constraints_val.IsNull() && constraints_val.type == JsonType::kArray) {
    for (size_t i = 0; i < constraints_val.array_val.size(); ++i) {
      const auto& c = constraints_val.array_val[i];
      if (c.type == JsonType::kString) {
        options.constraint_expressions.push_back(c.string_val);
      }
    }
  }

  // Parse sub-models.
  const auto& sub_models_val = json["subModels"];
  if (!sub_models_val.IsNull() && sub_models_val.type == JsonType::kArray) {
    for (size_t i = 0; i < sub_models_val.array_val.size(); ++i) {
      const auto& sm = sub_models_val.array_val[i];
      if (sm.type == JsonType::kObject) {
        coverwise::core::SubModel sub_model;
        const auto& sm_strength = sm["strength"];
        if (!sm_strength.IsNull() && sm_strength.type == JsonType::kNumber) {
          sub_model.strength = static_cast<uint32_t>(sm_strength.number_val);
        }
        const auto& sm_params = sm["parameters"];
        if (!sm_params.IsNull() && sm_params.type == JsonType::kArray) {
          for (const auto& p : sm_params.array_val) {
            if (p.type == JsonType::kString) {
              sub_model.parameter_names.push_back(p.string_val);
            }
          }
        }
        options.sub_models.push_back(std::move(sub_model));
      }
    }
  }

  auto stats = coverwise::core::EstimateModel(options);

  JsonWriter w(std::cout);
  w.BeginObject();

  w.Key("parameterCount");
  w.WriteNumber(static_cast<double>(stats.parameter_count));

  w.Key("totalValues");
  w.WriteNumber(static_cast<double>(stats.total_values));

  w.Key("strength");
  w.WriteNumber(static_cast<double>(stats.strength));

  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(stats.total_tuples));

  w.Key("estimatedTests");
  w.WriteNumber(static_cast<double>(stats.estimated_tests));

  w.Key("subModels");
  w.WriteNumber(static_cast<double>(stats.sub_model_count));

  w.Key("constraints");
  w.WriteNumber(static_cast<double>(stats.constraint_count));

  w.Key("parametersDetail");
  w.BeginArray();
  for (const auto& pd : stats.parameters) {
    w.Sep();
    w.BeginObject();
    w.Key("name");
    w.WriteString(pd.name);
    w.Key("valueCount");
    w.WriteNumber(static_cast<double>(pd.value_count));
    w.Key("invalidCount");
    w.WriteNumber(static_cast<double>(pd.invalid_count));
    w.EndObject();
  }
  w.EndArray();

  w.EndObject();
  std::cout << '\n';

  return kExitOk;
}

void PrintUsage() {
  std::cerr << "Usage:\n"
            << "  coverwise generate <input.json>\n"
            << "  coverwise analyze --params <params.json> --tests <tests.json>\n"
            << "  coverwise extend --existing <tests.json> <input.json>\n"
            << "  coverwise stats <input.json>\n"
            << "\n"
            << "Exit codes:\n"
            << "  0 = OK (coverage 100%)\n"
            << "  1 = Constraint error\n"
            << "  2 = Insufficient coverage\n"
            << "  3 = Invalid input\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  if (argc < 2) {
    PrintUsage();
    return kExitInvalidInput;
  }

  std::string command = argv[1];

  if (command == "--help" || command == "-h") {
    PrintUsage();
    return kExitOk;
  }

  if (command == "generate") {
    return RunGenerate(argc, argv);
  }

  if (command == "analyze") {
    return RunAnalyze(argc, argv);
  }

  if (command == "extend") {
    return RunExtend(argc, argv);
  }

  if (command == "stats") {
    return RunStats(argc, argv);
  }

  std::cerr << "Unknown command: " << command << "\n";
  PrintUsage();
  return kExitInvalidInput;
}
