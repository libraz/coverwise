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
#include <csignal>
#include <cstdint>
#include <cstdio>
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
#include "model/limits.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/surface_error.h"
#include "model/test_case.h"
#include "util/string_util.h"
#include "validator/coverage_validator.h"

namespace {

// The documented input limits live in model/limits.h and are the same on every
// surface; the CLI reads them from there instead of restating the numbers.
using coverwise::model::kMaxConstraints;
using coverwise::model::kMaxDocumentBytes;
using coverwise::model::kMaxParameters;
using coverwise::model::kMaxStringBytes;
using coverwise::model::kMaxTests;
using coverwise::model::kMaxValuesPerParameter;

/// @brief Cap on members in one JSON object, guarding the parser's key set.
///
/// Structural, not a documented input limit: it bounds what the reader will
/// build before any acceptance rule sees it.
constexpr size_t kMaxObjectMembers = 16384;

/// @brief Cap on how deeply values may nest in one JSON document.
///
/// Structural, not a documented input limit: the reader descends once per level,
/// so this is what keeps a deliberately nested document from exhausting the
/// stack before any acceptance rule sees it. Its equality with kMaxConstraints
/// is a coincidence of two unrelated bounds rather than one shared limit.
constexpr size_t kMaxNestingDepth = 256;

// Every failure a caller sees — its exit code and its message alike — is read
// off a SurfaceError, which only a model::Error can produce. No subcommand can
// name an exit code: ExitStatus has no constructor taking one.
using coverwise::model::ExitStatus;
using coverwise::model::SurfaceError;

/// @brief Print a failure in the CLI's stderr envelope and surface its status.
ExitStatus Fail(const coverwise::model::Error& error) {
  SurfaceError surfaced(error);
  std::cerr << "error: " << surfaced.text() << "\n";
  return surfaced;
}

/// @brief The failure a reader reports: an argument, a file, or a JSON document
/// the CLI could not turn into a model is invalid input by definition.
///
/// Readers hand this back rather than a bare string so a message never travels
/// separately from the code and detail it belongs to — the composition into user
/// text happens once, in SurfaceError, at the surface.
coverwise::model::Error ReaderError(std::string message) {
  return {coverwise::model::Error::Code::kInvalidInput, std::move(message), ""};
}

/// @brief Report a failure the core never saw. Routing it through a structured
/// Error keeps its message and its exit code paired the same way a core
/// failure's are.
ExitStatus InvalidInput(std::string message) { return Fail(ReaderError(std::move(message))); }

/// @brief Flush standard output and turn a failed write into a failure status.
///
/// A report is delivered only if its bytes reached the caller, and standard
/// output fails late: the writes that fail are the ones the standard library
/// makes on its own schedule, so a reader that closed the pipe (EPIPE), a
/// filesystem with no room left (ENOSPC) and a destination the caller never
/// opened (EBADF) all surface here, long after the last insertion returned.
/// A command that ends without looking would exit 0 beside a truncated
/// document, and a caller gating on the exit code reads that document as a
/// complete result.
/// Both the C++ stream and the C stream underneath it are asked, because which
/// of the two records the failed write depends on whether the two are still
/// synchronized — a detail of the standard library, not of the report.
ExitStatus FinishOutput(ExitStatus status) {
  std::cout.flush();
  const bool written =
      static_cast<bool>(std::cout) && std::fflush(stdout) == 0 && std::ferror(stdout) == 0;
  if (!written) {
    return InvalidInput("cannot write to standard output");
  }
  return status;
}

/// @brief Print usage text and classify the invocation as invalid input.
///
/// Usage text is its own envelope, without the "error: " prefix, so it is
/// printed here rather than through Fail.
ExitStatus UsageError(const std::string& usage) {
  std::cerr << usage;
  return SurfaceError({coverwise::model::Error::Code::kInvalidInput, usage, ""});
}

/// @brief Derive a subcommand's status from a generator result.
///
/// A structured failure takes precedence (a constraint error is exit 1).
/// Otherwise any coverage below 100% — for any reason, independent of maxTests
/// — is insufficient coverage. That shortfall is reported in the JSON body
/// rather than on stderr, so it is turned into a status without printing.
ExitStatus ResultStatus(const coverwise::model::GenerateResult& result) {
  if (!result.error.ok()) {
    return SurfaceError(result.error);
  }
  if (result.coverage < 1.0) {
    return SurfaceError(
        {coverwise::model::Error::Code::kInsufficientCoverage, "Coverage is below 100%", ""});
  }
  return ExitStatus::Success();
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
    if (depth > kMaxNestingDepth) {
      error_ = "JSON nesting depth exceeds " + std::to_string(kMaxNestingDepth);
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
    // The per-string limit is not applied here. This reader knows only that it
    // is inside some string, so it can say nothing about which one — and a
    // caller reading "string exceeds 65536 UTF-8 bytes" about a document with
    // thousands of them is told the limit without being told where they met it.
    // The readers above name the field, and the acceptance gate names the model
    // string, both in the model layer's own words.
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
// Input reading utility.
// ---------------------------------------------------------------------------

/// @brief True when @p path selects standard input rather than a named file.
bool IsStdinPath(const std::string& path) { return path == "-"; }

/// @brief Human-readable name of an input source, for diagnostics.
std::string InputName(const std::string& path) {
  return IsStdinPath(path) ? "standard input" : "file '" + path + "'";
}

/// @brief Read entire file contents into @p out.
/// @return true if the file was opened and read within the size cap.
bool ReadFile(const std::string& path, std::string& out, bool& too_large) {
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return false;
  const std::streampos size = file.tellg();
  if (size < 0) return false;
  if (static_cast<uint64_t>(size) > kMaxDocumentBytes) {
    too_large = true;
    return false;
  }
  out.resize(static_cast<size_t>(size));
  file.seekg(0);
  file.read(out.data(), static_cast<std::streamsize>(out.size()));
  if (!file && !out.empty()) return false;
  return true;
}

/// @brief Read standard input to end-of-stream into @p out.
///
/// Stops as soon as the size cap is exceeded so a runaway producer cannot make
/// the CLI allocate without bound.
/// @return true if the stream was consumed within the size cap.
bool ReadStdin(std::string& out, bool& too_large) {
  char buffer[65536];
  while (std::cin.read(buffer, sizeof(buffer)) || std::cin.gcount() > 0) {
    const size_t count = static_cast<size_t>(std::cin.gcount());
    if (out.size() + count > kMaxDocumentBytes) {
      too_large = true;
      return false;
    }
    out.append(buffer, count);
  }
  return !std::cin.bad();
}

/// @brief Read an input source named on the command line into @p out.
///
/// A path of `-` reads standard input, which every subcommand accepts wherever
/// it takes a file, so callers can pipe JSON instead of writing a temporary
/// file. Standard input is consumable only once, so a second `-` in the same
/// invocation is rejected rather than silently yielding empty content.
///
/// Every failure mode gets its own message in @p error: a source that cannot be
/// opened, one that exceeds the size cap, and one that is genuinely empty are
/// distinguishable instead of conflated.
/// @return true if non-empty content was read; otherwise @p error is set.
bool ReadInput(const std::string& path, std::string& out, std::string& error) {
  bool too_large = false;
  if (IsStdinPath(path)) {
    // A completed ReadStdin leaves eofbit set, so the stream itself records
    // whether an earlier argument already drained it — no separate flag needed.
    if (std::cin.eof()) {
      error = "standard input can only be read once; '-' was given more than once";
      return false;
    }
    if (!ReadStdin(out, too_large)) {
      error = too_large ? "standard input exceeds the maximum of " +
                              std::to_string(kMaxDocumentBytes) + " bytes"
                        : "cannot read standard input";
      return false;
    }
  } else if (!ReadFile(path, out, too_large)) {
    error = too_large ? "file '" + path + "' exceeds the maximum of " +
                            std::to_string(kMaxDocumentBytes) + " bytes"
                      : "cannot open file '" + path + "'";
    return false;
  }
  if (out.empty()) {
    error = InputName(path) + " is empty";
    return false;
  }
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

/// @brief How strictly a row array is held to the declared parameter set.
enum class TestRowPolicy {
  /// @brief `seeds`: the row's key set must equal the declared parameter names
  /// and every value must resolve, because a seed is asserted to be a real test
  /// case that generation will build on.
  kSeed,
  /// @brief `tests` / `existing`: a row that no longer matches the model is
  /// carried through with the mismatching positions left unassigned. The
  /// coverage validator classifies it and reports it as an invalid row, so a
  /// single drifted row costs its own coverage rather than the whole run.
  kRecorded,
};

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
    if (policy == TestRowPolicy::kSeed) {
      for (const auto& key : t.object_keys) {
        bool declared = false;
        for (const auto& param : params) {
          if (param.name == key) {
            declared = true;
            break;
          }
        }
        if (!declared) {
          error =
              std::string(field) + " " + std::to_string(i) + " has unknown parameter '" + key + "'";
          return false;
        }
      }
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
      const auto& val = t[params[pi].name];
      if (val.IsNull()) {
        if (policy == TestRowPolicy::kRecorded) continue;
        error = std::string(field) + " " + std::to_string(i) + " missing parameter '" +
                params[pi].name + "'";
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

// ---------------------------------------------------------------------------
// Write coverwise output as JSON.
// ---------------------------------------------------------------------------

/// @brief Write a parsed scalar JSON value back out unchanged.
void WriteScalar(JsonWriter& w, const JsonValue& value) {
  switch (value.type) {
    case JsonType::kString:
      w.WriteString(value.string_val);
      return;
    case JsonType::kNumber:
      w.WriteNumber(value.number_val);
      return;
    case JsonType::kBool:
      w.WriteBool(value.bool_val);
      return;
    default:
      w.WriteNull();
      return;
  }
}

/// @brief Write the result's tests, echoing a preserved prefix verbatim.
///
/// @param preserved_rows Rows the caller handed to extend, or nullptr. Extend
///        keeps them byte-for-byte, including members the model no longer
///        declares, so they are echoed from the input rather than rendered from
///        value indices — rendering would drop exactly the parts that make a
///        drifted row recognizable.
void WriteResultTests(JsonWriter& w, const coverwise::model::GenerateResult& result,
                      const std::vector<coverwise::model::Parameter>& params,
                      const JsonValue* preserved_rows) {
  const size_t preserved_count = preserved_rows == nullptr ? 0 : preserved_rows->array_val.size();
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    w.Sep();
    if (ti < preserved_count) {
      const auto& row = preserved_rows->array_val[ti];
      w.BeginObject();
      for (size_t k = 0; k < row.object_keys.size(); ++k) {
        w.Key(row.object_keys[k]);
        WriteScalar(w, row.object_vals[k]);
      }
      w.EndObject();
      continue;
    }
    const auto& tc = result.tests[ti];
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
      if (tc.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
    }
    w.EndObject();
  }
}

/// @param preserved_rows The raw `existing` rows extend must echo unchanged, or
///        nullptr for subcommands that render every row from the model.
void WriteGenerateResult(const coverwise::model::GenerateResult& result,
                         const std::vector<coverwise::model::Parameter>& params, uint32_t strength,
                         const JsonValue* preserved_rows = nullptr) {
  JsonWriter w(std::cout);
  w.BeginObject();
  w.Key("schemaVersion");
  w.WriteNumber(1);

  // tests
  w.Key("tests");
  w.BeginArray();
  WriteResultTests(w, result, params, preserved_rows);
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
    w.WriteString(SurfaceError(result.error).text());
    w.EndObject();
  }

  w.EndObject();
  std::cout << '\n';
}

/// @brief Emit a structured result error to stderr, if any. Keeps the failure
/// reason visible on the error stream (not just buried in the JSON warnings),
/// consistent with the analyze subcommand and the WASM surface.
void ReportResultError(const coverwise::model::GenerateResult& result) {
  if (result.error.ok()) return;
  std::cerr << "error: " << SurfaceError(result.error).text() << "\n";
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

ExitStatus RunGenerate(int argc, char* argv[]) {
  if (argc != 3) {
    return UsageError("Usage: coverwise generate <input.json>\n");
  }

  std::string content;
  std::string read_error;
  if (!ReadInput(argv[2], content, read_error)) {
    return InvalidInput(read_error);
  }

  JsonParser parser(content);
  auto json = parser.Parse();
  if (!parser.error().empty()) {
    return InvalidInput("invalid JSON: " + parser.error());
  }
  if (json.type != JsonType::kObject) {
    return InvalidInput("input must be a JSON object");
  }

  coverwise::model::ChargedTextReader budget;
  coverwise::model::GenerateOptions options;
  if (auto error = ParseModelDocument(json, options, budget); !error.ok()) {
    return Fail(error);
  }

  const uint32_t strength = options.strength;
  // The row text read above and the model's own strings are one input, so the
  // gate judges them against one budget.
  auto accepted = coverwise::model::AcceptOptions(std::move(options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  // Generate.
  auto result = coverwise::core::Generate(accepted->get());

  const auto& effective_params =
      result.parameters.empty() ? accepted->get().parameters : result.parameters;

  // Determine status: constraint error (1) > insufficient coverage (2) > OK.
  ExitStatus status = ResultStatus(result);

  WriteGenerateResult(result, effective_params, strength);
  ReportResultError(result);
  return FinishOutput(status);
}

ExitStatus RunAnalyze(int argc, char* argv[]) {
  // Parse flags.
  std::string params_path;
  std::string tests_path;
  std::string constraints_path;
  uint32_t strength = 2;
  bool strength_from_flag = false;

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
        return InvalidInput("--strength must be a positive integer (>= 1)");
      }
      strength = static_cast<uint32_t>(parsed);
      strength_from_flag = true;
    } else {
      return InvalidInput("unknown argument '" + arg + "'");
    }
  }

  if (params_path.empty() || tests_path.empty()) {
    return UsageError(
        "Usage: coverwise analyze --params <params.json> --tests <tests.json>"
        " [--strength <n>] [--constraints <file.json>]\n");
  }

  // Read and parse params. The params file may be either a bare array of
  // parameters or an object with a "parameters" array (and optional
  // "constraints"), matching the shape that generate accepts.
  std::string params_content;
  std::string read_error;
  if (!ReadInput(params_path, params_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser params_parser(params_content);
  auto params_json = params_parser.Parse();
  if (!params_parser.error().empty()) {
    return InvalidInput("invalid params JSON: " + params_parser.error());
  }

  std::string error;
  const JsonValue* params_array = &params_json;
  std::vector<std::string> constraint_expressions;
  if (params_json.type == JsonType::kObject) {
    params_array = &params_json["parameters"];
    if (!ParseConstraintExpressions(params_json["constraints"], constraint_expressions, error)) {
      return InvalidInput(error);
    }
    // The documented pipeline pipes one model through generate and then
    // analyze, so a field of that model which defines the coverage universe has
    // to reach the measurement. An explicit --strength wins, because it is an
    // analysis knob the caller chose for this run.
    if (!strength_from_flag && !ParseOptionalUint32(params_json, "strength", 1, strength, error)) {
      return InvalidInput(error);
    }
    // Sub-models give parts of the model their own strength, which the coverage
    // validator has no way to express: it measures one universe at one strength.
    // Measuring such a model as if the sub-models were absent would report a
    // ratio for a universe the caller never described, so it is refused before
    // any report is written. Analyze one sub-model at a time with --strength.
    if (!params_json["subModels"].IsNull()) {
      return InvalidInput(
          "analyze cannot measure a model with 'subModels'; analyze each group separately with"
          " --strength");
    }
  }

  // Analyze uses the same effective boundary-expanded value space as generate.
  // Its --strength is an analysis parameter rather than a property of the model,
  // so the model goes to the acceptance gate at strength 1 and the gate judges
  // the model alone. The requested strength is judged by ValidateCoverage, which
  // is fail-closed about it on every surface: 0, or more than the parameter
  // count, is invalid input rather than a coverage claim over an empty tuple
  // universe.
  coverwise::model::GenerateOptions model_options;
  model_options.strength = 1;
  if (auto model_error = ParseModelParameters(*params_array, model_options); !model_error.ok()) {
    return Fail(model_error);
  }
  std::vector<coverwise::model::Parameter> params = model_options.parameters;

  // Read constraints from a dedicated file if provided. A constraints file is a
  // JSON object with a "constraints" array (or a bare array of strings).
  if (!constraints_path.empty()) {
    std::string constraints_content;
    if (!ReadInput(constraints_path, constraints_content, read_error)) {
      return InvalidInput(read_error);
    }
    JsonParser constraints_parser(constraints_content);
    auto constraints_json = constraints_parser.Parse();
    if (!constraints_parser.error().empty()) {
      return InvalidInput("invalid constraints JSON: " + constraints_parser.error());
    }
    // An explicit --constraints replaces whatever --params declared, so a
    // document this reader cannot turn into a constraint list would erase the
    // model's constraints instead of failing. `jq '.constraints'` emits bare
    // null for a model that has none, which is exactly how such a document
    // reaches here, and a silently unconstrained run reports a coverage
    // shortfall that the caller has no error output to explain.
    if (constraints_json.type != JsonType::kArray && constraints_json.type != JsonType::kObject) {
      return InvalidInput(
          "constraints file must be a JSON array of expressions or an object with a 'constraints'"
          " array");
    }
    if (constraints_json.type == JsonType::kObject &&
        (!constraints_json.HasKey("constraints") ||
         constraints_json["constraints"].type != JsonType::kArray)) {
      return InvalidInput("constraints file object must have a 'constraints' JSON array");
    }
    const JsonValue& constraints_node = constraints_json.type == JsonType::kObject
                                            ? constraints_json["constraints"]
                                            : constraints_json;
    constraint_expressions.clear();
    if (!ParseConstraintExpressions(constraints_node, constraint_expressions, error)) {
      return InvalidInput(error);
    }
  }

  // Read and parse tests.
  std::string tests_content;
  if (!ReadInput(tests_path, tests_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser tests_parser(tests_content);
  auto tests_json = tests_parser.Parse();
  if (!tests_parser.error().empty()) {
    return InvalidInput("invalid tests JSON: " + tests_parser.error());
  }

  // Rows that no longer match the model are carried through with the
  // mismatching positions unassigned; ValidateCoverage classifies them into
  // invalidTests so the report covers the whole suite instead of stopping at
  // the first drifted row.
  std::vector<coverwise::model::TestCase> tests;
  const JsonValue* tests_array = nullptr;
  coverwise::model::ChargedTextReader budget;
  if (!ExtractTestsArray(tests_json, tests_array, error) ||
      !ParseTests(*tests_array, params, TestRowPolicy::kRecorded, "tests", tests, budget, error)) {
    return InvalidInput(error);
  }

  model_options.constraint_expressions = constraint_expressions;
  auto accepted = coverwise::model::AcceptOptions(std::move(model_options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  // Parse constraint expressions into AST so the validator can exclude
  // constraint-invalid tuples from the coverage universe, consistent with the
  // generator and the WASM/JS surfaces.
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expr : constraint_expressions) {
    auto parse_result = coverwise::model::ParseConstraint(expr, params);
    if (!parse_result.error.ok()) {
      return Fail(coverwise::model::AnnotateConstraintError(expr, parse_result.error));
    }
    constraints.push_back(std::move(parse_result.constraint));
  }

  auto report = coverwise::validator::ValidateCoverage(params, tests, strength, constraints);
  if (!report.error.ok()) {
    return Fail(report.error);
  }

  WriteCoverageReport(report);

  // A suite that contains invalid rows (out-of-range, invalid-value, or
  // constraint-violating tests) is malformed input, so exit with invalid-input
  // even when the valid subset covers everything. The details are still in the
  // JSON body (invalidTests). This takes precedence over the coverage shortfall.
  if (!report.invalid_tests.empty()) {
    return FinishOutput(InvalidInput(std::to_string(report.invalid_tests.size()) +
                                     " invalid test(s) in the analyzed suite"));
  }
  if (report.coverage_ratio < 1.0) {
    return FinishOutput(SurfaceError(
        {coverwise::model::Error::Code::kInsufficientCoverage, "Coverage is below 100%", ""}));
  }
  return FinishOutput(ExitStatus::Success());
}

ExitStatus RunExtend(int argc, char* argv[]) {
  // Parse --existing flag and input file.
  std::string existing_path;
  std::string input_path;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--existing" && i + 1 < argc) {
      existing_path = argv[++i];
    } else if (input_path.empty() && (IsStdinPath(arg) || (!arg.empty() && arg[0] != '-'))) {
      input_path = arg;
    } else {
      return InvalidInput("unknown argument '" + arg + "'");
    }
  }

  if (existing_path.empty() || input_path.empty()) {
    return UsageError("Usage: coverwise extend --existing <tests.json> <input.json>\n");
  }

  // Read and parse input.
  std::string input_content;
  std::string read_error;
  if (!ReadInput(input_path, input_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser input_parser(input_content);
  auto input_json = input_parser.Parse();
  if (!input_parser.error().empty()) {
    return InvalidInput("invalid input JSON: " + input_parser.error());
  }
  if (input_json.type != JsonType::kObject) {
    return InvalidInput("input must be a JSON object");
  }

  // Extend reads the same model document as generate, so it accepts and rejects
  // exactly what generate does. The document is read up front, which also
  // expands the boundary value space before the --existing rows below are
  // resolved against it, so every row's value indices match the parameters that
  // generation and rendering share.
  std::string error;
  // One budget for the whole invocation: the model document's `seeds` and the
  // `--existing` suite are both caller row text handed to the same run.
  coverwise::model::ChargedTextReader budget;
  coverwise::model::GenerateOptions options;
  if (auto document_error = ParseModelDocument(input_json, options, budget); !document_error.ok()) {
    return Fail(document_error);
  }

  // Read and parse existing tests.
  std::string existing_content;
  if (!ReadInput(existing_path, existing_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser existing_parser(existing_content);
  auto existing_json = existing_parser.Parse();
  if (!existing_parser.error().empty()) {
    return InvalidInput("invalid existing tests JSON: " + existing_parser.error());
  }

  // A recorded suite drifts from the model it was written against: a value gets
  // renamed, a parameter is added. Filling the gap in the model is the whole
  // point of extend, so a drifted row is carried through rather than failing the
  // run. Extend keeps the row as written, the core records why it could not be
  // counted, and the coverage figure is computed without it.
  std::vector<coverwise::model::TestCase> existing;
  const JsonValue* existing_array = nullptr;
  if (!ExtractTestsArray(existing_json, existing_array, error) ||
      !ParseTests(*existing_array, options.parameters, TestRowPolicy::kRecorded, "existing",
                  existing, budget, error)) {
    return InvalidInput(error);
  }

  const uint32_t strength = options.strength;
  auto accepted = coverwise::model::AcceptOptions(std::move(options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  auto result = coverwise::core::Extend(existing, accepted->get());

  const auto& effective_params =
      result.parameters.empty() ? accepted->get().parameters : result.parameters;

  // Determine status: constraint error (1) > insufficient coverage (2) > OK.
  ExitStatus status = ResultStatus(result);

  WriteGenerateResult(result, effective_params, strength, existing_array);
  ReportResultError(result);
  return FinishOutput(status);
}

ExitStatus RunStats(int argc, char* argv[]) {
  if (argc != 3) {
    return UsageError("Usage: coverwise stats <input.json>\n");
  }

  std::string content;
  std::string read_error;
  if (!ReadInput(argv[2], content, read_error)) {
    return InvalidInput(read_error);
  }

  JsonParser parser(content);
  auto json = parser.Parse();
  if (!parser.error().empty()) {
    return InvalidInput("invalid JSON: " + parser.error());
  }
  if (json.type != JsonType::kObject) {
    return InvalidInput("input must be a JSON object");
  }

  // Stats is a preflight for generate, so it reads the document through the same
  // reader: a document generate would refuse must not be reported on as if it
  // were a model. None of the figures below is derived from the fields that only
  // generation uses.
  coverwise::model::ChargedTextReader budget;
  coverwise::model::GenerateOptions options;
  if (auto error = ParseModelDocument(json, options, budget); !error.ok()) {
    return Fail(error);
  }

  auto accepted = coverwise::model::AcceptOptions(std::move(options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  auto stats = coverwise::core::EstimateModel(accepted->get());
  if (!stats.error.ok()) {
    return Fail(stats.error);
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

  return FinishOutput(ExitStatus::Success());
}

const char* UsageText() {
  return "Usage:\n"
         "  coverwise generate <input.json>\n"
         "  coverwise analyze --params <params.json> --tests <tests.json>"
         " [--strength <n>] [--constraints <file.json>]\n"
         "  coverwise extend --existing <tests.json> <input.json>\n"
         "  coverwise stats <input.json>\n"
         "\n"
         "Any input path may be '-' to read that JSON from standard input.\n"
         "\n"
         "Exit codes:\n"
         "  0 = OK (coverage 100%)\n"
         "  1 = Constraint error\n"
         "  2 = Insufficient coverage\n"
         "  3 = Invalid input\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  // `coverwise generate model.json | head` closes the pipe while the report is
  // still being written. The default disposition of SIGPIPE would end the
  // process on the signal, past every exit code this CLI documents and with
  // nothing on stderr, so a caller could not tell a reader that stopped early
  // from a real failure. Ignoring it turns the same event into an EPIPE the
  // write path reports, which FinishOutput reads and converts into exit 3.
  // The macro is POSIX; where it is absent there is no such signal to disarm.
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  try {
    if (argc < 2) {
      return UsageError(UsageText()).exit_code();
    }

    std::string command = argv[1];

    // Usage asked for is information the caller requested and the command
    // produced, so it goes to standard output, where `--help > help.txt` and
    // `--help | grep` can read it. Usage printed because an invocation was
    // wrong is a diagnostic and stays on standard error.
    if (command == "--help" || command == "-h") {
      std::cout << UsageText();
      return FinishOutput(ExitStatus::Success()).exit_code();
    }

    if (command == "generate") {
      return RunGenerate(argc, argv).exit_code();
    }

    if (command == "analyze") {
      return RunAnalyze(argc, argv).exit_code();
    }

    if (command == "extend") {
      return RunExtend(argc, argv).exit_code();
    }

    if (command == "stats") {
      return RunStats(argc, argv).exit_code();
    }

    std::cerr << "Unknown command: " << command << "\n";
    return UsageError(UsageText()).exit_code();
  } catch (const std::exception& error) {
    return InvalidInput(error.what()).exit_code();
  } catch (...) {
    return InvalidInput("unexpected failure").exit_code();
  }
}
