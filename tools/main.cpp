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
///   2 = Insufficient coverage (coverage < 100% for any reason)
///   3 = Invalid input

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include "core/generator.h"
#include "model/boundary.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "util/string_util.h"
#include "validator/coverage_validator.h"

namespace {

constexpr int kExitOk = 0;
constexpr int kExitConstraintError = 1;
constexpr int kExitInsufficientCoverage = 2;
constexpr int kExitInvalidInput = 3;
constexpr size_t kMaxInputBytes = 1 * 1024 * 1024;
constexpr size_t kMaxParameters = 1024;
constexpr size_t kMaxValuesPerParameter = 16384;
constexpr size_t kMaxTests = 100000;
constexpr size_t kMaxConstraints = 256;
constexpr size_t kMaxStringBytes = 64 * 1024;
constexpr size_t kMaxObjectMembers = 16384;

/// @brief Map a structured Error::Code to the documented CLI exit code.
///
/// This is the single mapping used by every subcommand so exit codes never
/// diverge: constraint errors are exit 1, invalid input and tuple explosion are
/// exit 3, insufficient coverage is exit 2, and ok is 0. The raw enum value is
/// never returned directly (kTupleExplosion == 4 would otherwise leak an
/// undocumented exit code).
int ErrorExitCode(coverwise::model::Error::Code code) {
  using Code = coverwise::model::Error::Code;
  switch (code) {
    case Code::kConstraintError:
      return kExitConstraintError;
    case Code::kInvalidInput:
    case Code::kTupleExplosion:
      return kExitInvalidInput;
    case Code::kInsufficientCoverage:
      return kExitInsufficientCoverage;
    case Code::kOk:
      return kExitOk;
  }
  return kExitOk;
}

/// @brief Map a generator result's error/coverage to a CLI exit code.
///
/// A constraint parse error takes precedence (exit 1). Otherwise, any coverage
/// below 100% — for any reason, independent of max_tests — yields insufficient
/// coverage (exit 2). A fully covered suite yields OK (exit 0). Invalid-input
/// errors surfaced in the result map to exit 3.
int ResultExitCode(const coverwise::model::GenerateResult& result) {
  if (result.error.code != coverwise::model::Error::Code::kOk) {
    return ErrorExitCode(result.error.code);
  }
  if (result.coverage < 1.0) {
    return kExitInsufficientCoverage;
  }
  return kExitOk;
}

/// @brief Validate that an interaction strength is a positive integer.
/// @return true if valid; otherwise prints a message to stderr and returns false.
bool ValidateStrength(uint32_t strength) {
  if (strength < 1) {
    std::cerr << "error: strength must be a positive integer (>= 1)\n";
    return false;
  }
  return true;
}

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
  bool HasKey(const std::string& key) const {
    return std::find(object_keys.begin(), object_keys.end(), key) != object_keys.end();
  }
};

/// @brief Append one strict UTF-8 code point from a raw JSON string.
///
/// JSON text is Unicode; accepting malformed UTF-8 here would let the writer
/// later emit a syntactically invalid JSON success result.
bool AppendUtf8CodePoint(const std::string& input, size_t& pos, std::string& output,
                         std::string& error) {
  const size_t start = pos;
  const auto byte_at = [&](size_t offset) { return static_cast<unsigned char>(input[offset]); };
  const unsigned char first = byte_at(pos);
  size_t length = 0;
  if (first <= 0x7F) {
    output.push_back(static_cast<char>(first));
    ++pos;
    return true;
  }
  if (first >= 0xC2 && first <= 0xDF) {
    length = 2;
  } else if (first >= 0xE0 && first <= 0xEF) {
    length = 3;
  } else if (first >= 0xF0 && first <= 0xF4) {
    length = 4;
  } else {
    error = "invalid UTF-8 leading byte at position " + std::to_string(start);
    return false;
  }
  if (pos + length > input.size()) {
    error = "truncated UTF-8 sequence at position " + std::to_string(start);
    return false;
  }
  for (size_t i = 1; i < length; ++i) {
    if ((byte_at(pos + i) & 0xC0) != 0x80) {
      error = "invalid UTF-8 continuation byte at position " + std::to_string(pos + i);
      return false;
    }
  }
  const unsigned char second = byte_at(pos + 1);
  if ((first == 0xE0 && second < 0xA0) || (first == 0xED && second > 0x9F) ||
      (first == 0xF0 && second < 0x90) || (first == 0xF4 && second > 0x8F)) {
    error = "invalid UTF-8 code point at position " + std::to_string(start);
    return false;
  }
  output.append(input, pos, length);
  pos += length;
  return true;
}

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
    if (pos_ != input_.size()) {
      error_ = "trailing characters after JSON value at position " + std::to_string(pos_);
      return {};
    }
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

  JsonValue ParseValue(size_t depth = 0) {
    if (depth > 256) {
      error_ = "JSON nesting depth exceeds 256";
      return {};
    }
    SkipWhitespace();
    char c = Peek();
    if (c == '"') return ParseString();
    if (c == '{') return ParseObject(depth + 1);
    if (c == '[') return ParseArray(depth + 1);
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
          case 'b':
            val.string_val += '\b';
            break;
          case 'f':
            val.string_val += '\f';
            break;
          case 'u': {
            // Parse 4 hex digits for a Unicode codepoint.
            if (pos_ + 4 > input_.size()) {
              error_ = "incomplete \\u escape at position " + std::to_string(pos_);
              return {};
            }
            auto parse_hex4 = [this]() -> uint32_t {
              uint32_t cp = 0;
              for (int h = 0; h < 4; ++h) {
                char hc = input_[pos_++];
                cp <<= 4;
                if (hc >= '0' && hc <= '9') {
                  cp |= static_cast<uint32_t>(hc - '0');
                } else if (hc >= 'a' && hc <= 'f') {
                  cp |= static_cast<uint32_t>(hc - 'a' + 10);
                } else if (hc >= 'A' && hc <= 'F') {
                  cp |= static_cast<uint32_t>(hc - 'A' + 10);
                } else {
                  error_ =
                      "invalid hex digit in \\u escape at position " + std::to_string(pos_ - 1);
                  return 0xFFFFFFFF;
                }
              }
              return cp;
            };
            uint32_t codepoint = parse_hex4();
            if (!error_.empty()) return {};
            // Handle surrogate pairs (high surrogate 0xD800-0xDBFF).
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
              if (pos_ + 6 > input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') {
                error_ = "missing low surrogate after high surrogate at position " +
                         std::to_string(pos_);
                return {};
              }
              pos_ += 2;  // skip \u
              uint32_t low = parse_hex4();
              if (!error_.empty()) return {};
              if (low < 0xDC00 || low > 0xDFFF) {
                error_ = "invalid low surrogate at position " + std::to_string(pos_ - 4);
                return {};
              }
              codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (low - 0xDC00);
            } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
              error_ = "unexpected low surrogate at position " + std::to_string(pos_ - 4);
              return {};
            }
            // Encode as UTF-8.
            if (codepoint <= 0x7F) {
              val.string_val += static_cast<char>(codepoint);
            } else if (codepoint <= 0x7FF) {
              val.string_val += static_cast<char>(0xC0 | (codepoint >> 6));
              val.string_val += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else if (codepoint <= 0xFFFF) {
              val.string_val += static_cast<char>(0xE0 | (codepoint >> 12));
              val.string_val += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
              val.string_val += static_cast<char>(0x80 | (codepoint & 0x3F));
            } else if (codepoint <= 0x10FFFF) {
              val.string_val += static_cast<char>(0xF0 | (codepoint >> 18));
              val.string_val += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
              val.string_val += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
              val.string_val += static_cast<char>(0x80 | (codepoint & 0x3F));
            }
            break;
          }
          default:
            error_ = std::string("invalid string escape \\") + esc + " at position " +
                     std::to_string(pos_ - 1);
            return {};
        }
      } else {
        unsigned char raw = static_cast<unsigned char>(input_[pos_]);
        if (raw <= 0x1F) {
          error_ = "unescaped control character in string at position " + std::to_string(pos_);
          return {};
        }
        if (!AppendUtf8CodePoint(input_, pos_, val.string_val, error_)) return {};
      }
    }
    if (pos_ >= input_.size()) {
      error_ = "unterminated string";
      return {};
    }
    ++pos_;  // skip closing "
    if (val.string_val.size() > kMaxStringBytes) {
      error_ = "string exceeds " + std::to_string(kMaxStringBytes) + " UTF-8 bytes";
      return {};
    }
    return val;
  }

  JsonValue ParseNumber() {
    size_t start = pos_;
    if (Peek() == '-') ++pos_;
    if (pos_ >= input_.size() || input_[pos_] < '0' || input_[pos_] > '9') {
      error_ = "invalid number at position " + std::to_string(start);
      return {};
    }
    if (input_[pos_] == '0') {
      ++pos_;
      if (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') {
        error_ = "leading zero in number at position " + std::to_string(start);
        return {};
      }
    } else {
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
    }
    if (pos_ < input_.size() && input_[pos_] == '.') {
      ++pos_;
      size_t fraction_start = pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
      if (pos_ == fraction_start) {
        error_ = "fraction requires digits at position " + std::to_string(pos_);
        return {};
      }
    }
    if (pos_ < input_.size() && (input_[pos_] == 'e' || input_[pos_] == 'E')) {
      ++pos_;
      if (pos_ < input_.size() && (input_[pos_] == '+' || input_[pos_] == '-')) ++pos_;
      size_t exponent_start = pos_;
      while (pos_ < input_.size() && input_[pos_] >= '0' && input_[pos_] <= '9') ++pos_;
      if (pos_ == exponent_start) {
        error_ = "exponent requires digits at position " + std::to_string(pos_);
        return {};
      }
    }
    JsonValue val;
    val.type = JsonType::kNumber;
    const std::string token = input_.substr(start, pos_ - start);
    char* end = nullptr;
    errno = 0;
    val.number_val = std::strtod(token.c_str(), &end);
    if (errno == ERANGE || end != token.c_str() + token.size() || !std::isfinite(val.number_val)) {
      error_ = "number is out of finite range at position " + std::to_string(start);
      return {};
    }
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

  JsonValue ParseArray(size_t depth) {
    if (!Expect('[')) return {};
    JsonValue val;
    val.type = JsonType::kArray;
    SkipWhitespace();
    if (Peek() == ']') {
      ++pos_;
      return val;
    }
    while (true) {
      val.array_val.push_back(ParseValue(depth));
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

  JsonValue ParseObject(size_t depth) {
    if (!Expect('{')) return {};
    JsonValue val;
    val.type = JsonType::kObject;
    SkipWhitespace();
    if (Peek() == '}') {
      ++pos_;
      return val;
    }
    std::unordered_set<std::string> seen_keys;
    while (true) {
      SkipWhitespace();
      auto key = ParseString();
      if (!error_.empty()) return {};
      if (!Expect(':')) return {};
      if (!seen_keys.insert(key.string_val).second) {
        error_ = "duplicate object key '" + key.string_val + "'";
        return {};
      }
      if (seen_keys.size() > kMaxObjectMembers) {
        error_ = "object has too many members";
        return {};
      }
      auto value = ParseValue(depth);
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

// ---------------------------------------------------------------------------
// JSON writer — produces compact JSON on stdout.
// ---------------------------------------------------------------------------

class JsonWriter {
 public:
  explicit JsonWriter(std::ostream& out) : out_(out) {}

  void WriteNull() { out_ << "null"; }

  void WriteBool(bool v) { out_ << (v ? "true" : "false"); }

  void WriteNumber(double v) {
    if (!std::isfinite(v)) {
      throw std::runtime_error("cannot serialize a non-finite JSON number");
    }
    // Use the shared ECMAScript Number-to-String algorithm so CLI numeric output
    // is byte-identical to the WASM / npm / pure-TS surfaces (shortest round-trip
    // form, no fixed-precision artifacts).
    out_ << coverwise::util::JsNumberToString(v);
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
        case '\b':
          out_ << "\\b";
          break;
        case '\f':
          out_ << "\\f";
          break;
        default:
          if (static_cast<unsigned char>(c) <= 0x1F) {
            const auto flags = out_.flags();
            const auto fill = out_.fill();
            out_ << "\\u" << std::hex << std::uppercase << std::setw(4) << std::setfill('0')
                 << static_cast<unsigned int>(static_cast<unsigned char>(c));
            out_.flags(flags);
            out_.fill(fill);
          } else {
            out_ << c;
          }
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

/// @brief Read entire file contents into @p out.
///
/// Distinguishes a file that cannot be opened (missing/unreadable) from one that
/// opens successfully but is genuinely empty: the former returns false, the latter
/// returns true with an empty @p out. This lets callers emit accurate diagnostics
/// ("cannot open file" vs "file is empty") instead of conflating the two.
/// @return true if the file was opened and read; false if it could not be opened.
bool ReadFile(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;
  const std::streampos size = file.tellg();
  if (size < 0 || static_cast<uint64_t>(size) > kMaxInputBytes) return false;
  out.resize(static_cast<size_t>(size));
  file.seekg(0);
  file.read(out.data(), static_cast<std::streamsize>(out.size()));
  if (!file && !out.empty()) return false;
  return true;
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

/// @brief Parse parameters and run structural validation.
///
/// Wraps ParseParameters with model::ValidateParameters so the CLI rejects
/// empty names, empty value lists, duplicate values within a parameter, and
/// duplicate parameter names — the same checks the WASM/JS surfaces enforce.
/// @return true on success; on failure sets error and returns false.
bool ParseAndValidateParameters(const JsonValue& json,
                                std::vector<coverwise::model::Parameter>& params,
                                std::string& error) {
  if (!ParseParameters(json, params, error)) {
    return false;
  }
  // Boundary-configured parameters may start with no explicit values because
  // their range supplies the value set. Use a temporary identity solely for
  // structural validation; the actual range is validated and expanded later.
  auto validation_params = params;
  for (size_t i = 0; i < validation_params.size(); ++i) {
    const auto& parameter_json = json.array_val[i];
    if (validation_params[i].values.empty() &&
        (parameter_json.HasKey("type") || parameter_json.HasKey("range") ||
         parameter_json.HasKey("step"))) {
      validation_params[i].values.push_back("__coverwise_boundary_placeholder__");
    }
  }
  auto validation = coverwise::model::ValidateParameters(validation_params);
  if (!validation.ok()) {
    error = validation.message;
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
      config.step = 1.0;
    } else if (type_val.string_val == "float") {
      config.type = coverwise::model::BoundaryConfig::Type::kFloat;
      const auto& step_val = p["step"];
      if (p.HasKey("step") && step_val.type != JsonType::kNumber) {
        error = "parameter '" + name_val.string_val + "' boundary step must be a number";
        return false;
      }
      if (step_val.type == JsonType::kNumber) {
        config.step = step_val.number_val;
      } else {
        config.step = 1.0;
      }
    } else {
      error = "parameter '" + name_val.string_val + "' has unknown boundary type";
      return false;
    }

    configs[name_val.string_val] = config;
  }
  return true;
}

/// @brief Expand numeric boundary parameters up front and clear the configs.
///
/// Boundary expansion must happen before generation so that a single Parameter
/// object (with expanded values) is the source of truth for both generation and
/// rendering. Otherwise test cases — which carry value indices — render against
/// the unexpanded parameters and produce empty/garbage values. After expanding,
/// the configs are cleared so core::Generate does not expand a second time.
bool ApplyBoundaryExpansion(coverwise::model::GenerateOptions& options, std::string& error) {
  if (options.boundary_configs.empty()) return true;
  auto validation = coverwise::model::ValidateGenerateOptions(options);
  if (!validation.ok()) {
    error = validation.message + (validation.detail.empty() ? "" : ": " + validation.detail);
    return false;
  }
  for (auto& param : options.parameters) {
    auto it = options.boundary_configs.find(param.name);
    if (it != options.boundary_configs.end()) {
      param = coverwise::model::ExpandBoundaryValues(param, it->second);
    }
  }
  options.boundary_configs.clear();
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
  if (json.array_val.size() > kMaxTests) {
    error = "tests exceed maximum of " + std::to_string(kMaxTests);
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
        val_str = coverwise::util::JsNumberToString(val.number_val);
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

// ---------------------------------------------------------------------------
// Write coverwise output as JSON.
// ---------------------------------------------------------------------------

void WriteGenerateResult(const coverwise::model::GenerateResult& result,
                         const std::vector<coverwise::model::Parameter>& params,
                         uint32_t strength) {
  JsonWriter w(std::cout);
  w.BeginObject();
  w.Key("schemaVersion");
  w.WriteNumber(1);

  // tests
  w.Key("tests");
  w.BeginArray();
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    const auto& tc = result.tests[ti];
    w.Sep();
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
      if (tc.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
    }
    w.EndObject();
  }
  w.EndArray();
  w.Key("uncoveredCount");
  w.WriteNumber(static_cast<double>(result.uncovered_count));
  w.Key("omittedUncovered");
  w.WriteNumber(static_cast<double>(result.omitted_uncovered));

  // negativeTests is required even when no invalid values are configured.
  w.Key("negativeTests");
  w.BeginArray();
  for (size_t ti = 0; ti < result.negative_tests.size(); ++ti) {
    const auto& tc = result.negative_tests[ti];
    w.Sep();
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
      if (tc.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
    }
    w.EndObject();
  }
  w.EndArray();
  if (result.negative_coverage) {
    w.Key("negativeCoverage");
    w.BeginObject();
    w.Key("totalTuples");
    w.WriteNumber(static_cast<double>(result.negative_coverage->total_tuples));
    w.Key("coveredTuples");
    w.WriteNumber(static_cast<double>(result.negative_coverage->covered_tuples));
    w.Key("omittedTuples");
    w.WriteNumber(static_cast<double>(result.negative_coverage->omitted_tuples));
    w.Key("coverageRatio");
    w.WriteNumber(result.negative_coverage->coverage_ratio);
    w.EndObject();
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
    w.Key("indices");
    w.BeginArray();
    for (const auto& [parameter_index, value_index] : u.indices) {
      w.Sep();
      w.BeginArray();
      w.Sep();
      w.WriteNumber(parameter_index);
      w.Sep();
      w.WriteNumber(value_index);
      w.EndArray();
    }
    w.EndArray();
    w.Key("reason");
    w.WriteString(u.reason);
    w.Key("display");
    w.WriteString(u.ToString());
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

  // classCoverage (only when equivalence classes are defined)
  if (result.class_coverage) {
    w.Key("classCoverage");
    w.BeginObject();
    w.Key("totalClassTuples");
    w.WriteNumber(static_cast<double>(result.class_coverage->total_class_tuples));
    w.Key("coveredClassTuples");
    w.WriteNumber(static_cast<double>(result.class_coverage->covered_class_tuples));
    w.Key("classCoverageRatio");
    w.WriteNumber(result.class_coverage->class_coverage_ratio);
    w.EndObject();
  }

  // suggestions
  w.Key("suggestions");
  w.BeginArray();
  for (const auto& s : result.suggestions) {
    w.Sep();
    w.BeginObject();
    w.Key("description");
    w.WriteString(s.description);
    w.Key("testCase");
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < s.test_case.values.size(); ++i) {
      if (s.test_case.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].values[s.test_case.values[i]]);
    }
    w.EndObject();
    w.EndObject();
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

  // error: emitted only on an early-exit failure (constraint parse error,
  // invalid input, tuple explosion), mirroring the WASM envelope's { code,
  // message } so CLI JSON consumers detect failures uniformly across surfaces.
  if (result.error.code != coverwise::model::Error::Code::kOk) {
    w.Key("error");
    w.BeginObject();
    w.Key("code");
    w.WriteNumber(static_cast<double>(static_cast<int>(result.error.code)));
    w.Key("message");
    std::string msg = result.error.message;
    if (!result.error.detail.empty()) msg += ": " + result.error.detail;
    w.WriteString(msg);
    w.EndObject();
  }

  w.EndObject();
  std::cout << '\n';
}

/// @brief Emit a structured result error to stderr, if any. Keeps the failure
/// reason visible on the error stream (not just buried in the JSON warnings),
/// consistent with the analyze subcommand and the WASM surface.
void ReportResultError(const coverwise::model::GenerateResult& result) {
  if (result.error.code == coverwise::model::Error::Code::kOk) return;
  std::cerr << "error: " << result.error.message;
  if (!result.error.detail.empty()) std::cerr << ": " << result.error.detail;
  std::cerr << "\n";
}

void WriteCoverageReport(const coverwise::validator::CoverageReport& report) {
  JsonWriter w(std::cout);
  w.BeginObject();
  w.Key("schemaVersion");
  w.WriteNumber(1);

  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(report.total_tuples));

  w.Key("coveredTuples");
  w.WriteNumber(static_cast<double>(report.covered_tuples));

  w.Key("coverageRatio");
  w.WriteNumber(report.coverage_ratio);

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
    w.Key("indices");
    w.BeginArray();
    for (const auto& [parameter_index, value_index] : u.indices) {
      w.Sep();
      w.BeginArray();
      w.Sep();
      w.WriteNumber(parameter_index);
      w.Sep();
      w.WriteNumber(value_index);
      w.EndArray();
    }
    w.EndArray();
    w.Key("reason");
    w.WriteString(u.reason);
    w.Key("display");
    w.WriteString(u.ToString());
    w.EndObject();
  }
  w.EndArray();

  w.Key("uncoveredCount");
  w.WriteNumber(static_cast<double>(report.uncovered_count));
  w.Key("omittedUncovered");
  w.WriteNumber(static_cast<double>(report.omitted_uncovered));
  w.Key("invalidTests");
  w.BeginArray();
  for (const auto& invalid : report.invalid_tests) {
    w.Sep();
    w.BeginObject();
    w.Key("testIndex");
    w.WriteNumber(static_cast<double>(invalid.test_index));
    w.Key("reason");
    w.WriteString(invalid.reason);
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
  if (argc != 3) {
    std::cerr << "Usage: coverwise generate <input.json>\n";
    return kExitInvalidInput;
  }

  std::string content;
  if (!ReadFile(argv[2], content)) {
    std::cerr << "error: cannot open file '" << argv[2] << "'\n";
    return kExitInvalidInput;
  }
  if (content.empty()) {
    std::cerr << "error: file '" << argv[2] << "' is empty\n";
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
  coverwise::model::GenerateOptions options;
  if (!ParseAndValidateParameters(json["parameters"], options.parameters, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse boundary value configs from parameters.
  if (!ParseBoundaryConfigs(json["parameters"], options.boundary_configs, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse optional fields.
  if (!ParseOptionalUint32(json, "strength", 1, options.strength, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  uint32_t seed = 0;
  if (!ParseOptionalUint32(json, "seed", 0, seed, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  options.seed = seed;
  if (!ParseOptionalUint32(json, "maxTests", 0, options.max_tests, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse constraints (array of strings).
  if (!ParseConstraintExpressions(json["constraints"], options.constraint_expressions, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse weights: {"param_name": {"value_name": weight, ...}, ...}
  if (!ParseWeights(json["weights"], options.weights, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse sub-models (mixed-strength parameter groups).
  if (!ParseSubModels(json["subModels"], options.sub_models, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Expand numeric boundary parameters up front so the same Parameter objects
  // drive both generation and rendering. Seed tests are parsed afterward so
  // their value indices match the expanded value lists.
  if (!ApplyBoundaryExpansion(options, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse seed tests (existing tests to build upon).
  const auto& seeds_val = json["seeds"];
  if (!seeds_val.IsNull()) {
    if (!ParseTests(seeds_val, options.parameters, options.seeds, error)) {
      std::cerr << "error: " << error << "\n";
      return kExitInvalidInput;
    }
  }

  // Generate.
  auto result = coverwise::core::Generate(options);

  const auto& effective_params = result.parameters.empty() ? options.parameters : result.parameters;

  // Determine exit code: constraint error (1) > insufficient coverage (2) > OK.
  int exit_code = ResultExitCode(result);

  WriteGenerateResult(result, effective_params, options.strength);
  ReportResultError(result);
  return exit_code;
}

int RunAnalyze(int argc, char* argv[]) {
  // Parse flags.
  std::string params_path;
  std::string tests_path;
  std::string constraints_path;
  uint32_t strength = 2;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--params" && i + 1 < argc) {
      params_path = argv[++i];
    } else if (arg == "--tests" && i + 1 < argc) {
      tests_path = argv[++i];
    } else if (arg == "--constraints" && i + 1 < argc) {
      constraints_path = argv[++i];
    } else if (arg == "--strength" && i + 1 < argc) {
      std::string value = argv[++i];
      char* end = nullptr;
      long parsed = std::strtol(value.c_str(), &end, 10);
      if (end == value.c_str() || *end != '\0' || parsed < 1 || parsed > UINT32_MAX) {
        std::cerr << "error: --strength must be a positive integer (>= 1)\n";
        return kExitInvalidInput;
      }
      strength = static_cast<uint32_t>(parsed);
    } else {
      std::cerr << "error: unknown argument '" << arg << "'\n";
      return kExitInvalidInput;
    }
  }

  if (params_path.empty() || tests_path.empty()) {
    std::cerr << "Usage: coverwise analyze --params <params.json> --tests <tests.json>"
              << " [--strength <n>] [--constraints <file.json>]\n";
    return kExitInvalidInput;
  }

  if (!ValidateStrength(strength)) {
    return kExitInvalidInput;
  }

  // Read and parse params. The params file may be either a bare array of
  // parameters or an object with a "parameters" array (and optional
  // "constraints"), matching the shape that generate accepts.
  std::string params_content;
  if (!ReadFile(params_path, params_content)) {
    std::cerr << "error: cannot open file '" << params_path << "'\n";
    return kExitInvalidInput;
  }
  if (params_content.empty()) {
    std::cerr << "error: file '" << params_path << "' is empty\n";
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
  const JsonValue* params_array = &params_json;
  std::vector<std::string> constraint_expressions;
  if (params_json.type == JsonType::kObject) {
    params_array = &params_json["parameters"];
    if (!ParseConstraintExpressions(params_json["constraints"], constraint_expressions, error)) {
      std::cerr << "error: " << error << "\n";
      return kExitInvalidInput;
    }
  }
  if (!ParseAndValidateParameters(*params_array, params, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Analyze uses the same effective boundary-expanded value space as generate.
  coverwise::model::GenerateOptions boundary_options;
  boundary_options.parameters = std::move(params);
  boundary_options.strength = strength;
  if (!ParseBoundaryConfigs(*params_array, boundary_options.boundary_configs, error) ||
      !ApplyBoundaryExpansion(boundary_options, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  params = std::move(boundary_options.parameters);

  // Read constraints from a dedicated file if provided. A constraints file is a
  // JSON object with a "constraints" array (or a bare array of strings).
  if (!constraints_path.empty()) {
    std::string constraints_content;
    if (!ReadFile(constraints_path, constraints_content)) {
      std::cerr << "error: cannot open file '" << constraints_path << "'\n";
      return kExitInvalidInput;
    }
    if (constraints_content.empty()) {
      std::cerr << "error: file '" << constraints_path << "' is empty\n";
      return kExitInvalidInput;
    }
    JsonParser constraints_parser(constraints_content);
    auto constraints_json = constraints_parser.Parse();
    if (!constraints_parser.error().empty()) {
      std::cerr << "error: invalid constraints JSON: " << constraints_parser.error() << "\n";
      return kExitInvalidInput;
    }
    if (constraints_json.type == JsonType::kObject &&
        (!constraints_json.HasKey("constraints") ||
         constraints_json["constraints"].type != JsonType::kArray)) {
      std::cerr << "error: constraints file object must have a 'constraints' JSON array\n";
      return kExitInvalidInput;
    }
    const JsonValue& constraints_node = constraints_json.type == JsonType::kObject
                                            ? constraints_json["constraints"]
                                            : constraints_json;
    constraint_expressions.clear();
    if (!ParseConstraintExpressions(constraints_node, constraint_expressions, error)) {
      std::cerr << "error: " << error << "\n";
      return kExitInvalidInput;
    }
  }

  // Read and parse tests.
  std::string tests_content;
  if (!ReadFile(tests_path, tests_content)) {
    std::cerr << "error: cannot open file '" << tests_path << "'\n";
    return kExitInvalidInput;
  }
  if (tests_content.empty()) {
    std::cerr << "error: file '" << tests_path << "' is empty\n";
    return kExitInvalidInput;
  }
  JsonParser tests_parser(tests_content);
  auto tests_json = tests_parser.Parse();
  if (!tests_parser.error().empty()) {
    std::cerr << "error: invalid tests JSON: " << tests_parser.error() << "\n";
    return kExitInvalidInput;
  }

  std::vector<coverwise::model::TestCase> tests;
  const JsonValue* tests_array = nullptr;
  if (!ExtractTestsArray(tests_json, tests_array, error) ||
      !ParseTests(*tests_array, params, tests, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse constraint expressions into AST so the validator can exclude
  // constraint-invalid tuples from the coverage universe, consistent with the
  // generator and the WASM/JS surfaces.
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expr : constraint_expressions) {
    auto parse_result = coverwise::model::ParseConstraint(expr, params);
    if (!parse_result.error.ok()) {
      std::cerr << "error: " << parse_result.error.message << ": " << parse_result.error.detail
                << "\n";
      return kExitConstraintError;
    }
    constraints.push_back(std::move(parse_result.constraint));
  }

  auto report = coverwise::validator::ValidateCoverage(params, tests, strength, constraints);
  if (!report.error.ok()) {
    std::cerr << "error: " << report.error.message << ": " << report.error.detail << "\n";
    return ErrorExitCode(report.error.code);
  }

  WriteCoverageReport(report);

  // A suite that contains invalid rows (out-of-range, invalid-value, or
  // constraint-violating tests) is malformed input, so exit with invalid-input
  // even when the valid subset covers everything. The details are still in the
  // JSON body (invalidTests). This takes precedence over the coverage shortfall.
  if (!report.invalid_tests.empty()) {
    std::cerr << "error: " << report.invalid_tests.size()
              << " invalid test(s) in the analyzed suite\n";
    return kExitInvalidInput;
  }
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
    } else if (!arg.empty() && arg[0] != '-' && input_path.empty()) {
      input_path = arg;
    } else {
      std::cerr << "error: unknown argument '" << arg << "'\n";
      return kExitInvalidInput;
    }
  }

  if (existing_path.empty() || input_path.empty()) {
    std::cerr << "Usage: coverwise extend --existing <tests.json> <input.json>\n";
    return kExitInvalidInput;
  }

  // Read and parse input.
  std::string input_content;
  if (!ReadFile(input_path, input_content)) {
    std::cerr << "error: cannot open file '" << input_path << "'\n";
    return kExitInvalidInput;
  }
  if (input_content.empty()) {
    std::cerr << "error: file '" << input_path << "' is empty\n";
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
  coverwise::model::GenerateOptions options;
  if (!ParseAndValidateParameters(input_json["parameters"], options.parameters, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse boundary value configs and expand up front so generation and
  // rendering share the same expanded Parameter objects. Seeds and existing
  // tests are parsed afterward so their value indices match.
  if (!ParseBoundaryConfigs(input_json["parameters"], options.boundary_configs, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  if (!ParseOptionalUint32(input_json, "strength", 1, options.strength, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  uint32_t seed = 0;
  if (!ParseOptionalUint32(input_json, "seed", 0, seed, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  options.seed = seed;
  if (!ParseOptionalUint32(input_json, "maxTests", 0, options.max_tests, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse constraints (array of strings).
  if (!ParseConstraintExpressions(input_json["constraints"], options.constraint_expressions,
                                  error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse weights: {"param_name": {"value_name": weight, ...}, ...}
  if (!ParseWeights(input_json["weights"], options.weights, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse sub-models (mixed-strength parameter groups).
  if (!ParseSubModels(input_json["subModels"], options.sub_models, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  if (!ApplyBoundaryExpansion(options, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse seed tests from the input JSON (in addition to --existing tests).
  const auto& seeds_val = input_json["seeds"];
  if (!seeds_val.IsNull()) {
    if (!ParseTests(seeds_val, options.parameters, options.seeds, error)) {
      std::cerr << "error: " << error << "\n";
      return kExitInvalidInput;
    }
  }

  // Read and parse existing tests.
  std::string existing_content;
  if (!ReadFile(existing_path, existing_content)) {
    std::cerr << "error: cannot open file '" << existing_path << "'\n";
    return kExitInvalidInput;
  }
  if (existing_content.empty()) {
    std::cerr << "error: file '" << existing_path << "' is empty\n";
    return kExitInvalidInput;
  }
  JsonParser existing_parser(existing_content);
  auto existing_json = existing_parser.Parse();
  if (!existing_parser.error().empty()) {
    std::cerr << "error: invalid existing tests JSON: " << existing_parser.error() << "\n";
    return kExitInvalidInput;
  }

  std::vector<coverwise::model::TestCase> existing;
  const JsonValue* existing_array = nullptr;
  if (!ExtractTestsArray(existing_json, existing_array, error) ||
      !ParseTests(*existing_array, options.parameters, existing, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  auto result = coverwise::core::Extend(existing, options);

  const auto& effective_params = result.parameters.empty() ? options.parameters : result.parameters;

  // Determine exit code: constraint error (1) > insufficient coverage (2) > OK.
  int exit_code = ResultExitCode(result);

  WriteGenerateResult(result, effective_params, options.strength);
  ReportResultError(result);
  return exit_code;
}

int RunStats(int argc, char* argv[]) {
  if (argc != 3) {
    std::cerr << "Usage: coverwise stats <input.json>\n";
    return kExitInvalidInput;
  }

  std::string content;
  if (!ReadFile(argv[2], content)) {
    std::cerr << "error: cannot open file '" << argv[2] << "'\n";
    return kExitInvalidInput;
  }
  if (content.empty()) {
    std::cerr << "error: file '" << argv[2] << "' is empty\n";
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
  coverwise::model::GenerateOptions options;
  if (!ParseAndValidateParameters(json["parameters"], options.parameters, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  // Parse boundary value configs from parameters.
  if (!ParseBoundaryConfigs(json["parameters"], options.boundary_configs, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  if (!ParseOptionalUint32(json, "strength", 1, options.strength, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  uint32_t seed = 0;
  if (!ParseOptionalUint32(json, "seed", 0, seed, error) ||
      !ParseOptionalUint32(json, "maxTests", 0, options.max_tests, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }
  options.seed = seed;

  if (!ParseConstraintExpressions(json["constraints"], options.constraint_expressions, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  if (!ParseSubModels(json["subModels"], options.sub_models, error) ||
      !ParseWeights(json["weights"], options.weights, error)) {
    std::cerr << "error: " << error << "\n";
    return kExitInvalidInput;
  }

  auto stats = coverwise::core::EstimateModel(options);
  if (!stats.error.ok()) {
    std::cerr << "error: " << stats.error.message;
    if (!stats.error.detail.empty()) std::cerr << ": " << stats.error.detail;
    std::cerr << "\n";
    return kExitInvalidInput;
  }

  JsonWriter w(std::cout);
  w.BeginObject();

  w.Key("schemaVersion");
  w.WriteNumber(1);

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

  w.Key("subModelCount");
  w.WriteNumber(static_cast<double>(stats.sub_model_count));

  w.Key("constraintCount");
  w.WriteNumber(static_cast<double>(stats.constraint_count));

  w.Key("parameters");
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
            << "  coverwise analyze --params <params.json> --tests <tests.json>"
               " [--strength <n>] [--constraints <file.json>]\n"
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
  try {
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
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return kExitInvalidInput;
  } catch (...) {
    std::cerr << "error: unexpected failure\n";
    return kExitInvalidInput;
  }
}
