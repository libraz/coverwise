/// @file json.h
/// @brief Minimal JSON value, parser and writer for the CLI.

#ifndef COVERWISE_TOOLS_JSON_H_
#define COVERWISE_TOOLS_JSON_H_

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "model/limits.h"
#include "util/string_util.h"

namespace coverwise {
namespace cli {

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

// ---------------------------------------------------------------------------
// Minimal JSON value representation — just enough for coverwise I/O.
// ---------------------------------------------------------------------------

enum class JsonType { kNull, kBool, kNumber, kString, kArray, kObject };

struct JsonMembers;

/// @brief One parsed JSON value.
///
/// Scalars live inline; array elements and object members sit behind one shared
/// pointer, so the common leaf node stays small. A parsed document is
/// immutable, which is why copies may share the member storage.
struct JsonValue {
  JsonType type = JsonType::kNull;
  bool bool_val = false;
  double number_val = 0.0;
  std::string string_val;
  std::shared_ptr<const JsonMembers> members;

  /// @brief Array elements, or object member values in insertion order.
  inline const std::vector<JsonValue>& Items() const;

  /// @brief Key of the object member at @p index.
  inline const std::string& KeyAt(size_t index) const;

  /// @brief Access an object member by key. Returns a null JsonValue if missing.
  const JsonValue& operator[](const std::string& key) const {
    const size_t index = IndexOfKey(key);
    if (index != kNoKey) return Items()[index];
    static const JsonValue kNull;
    return kNull;
  }

  /// @brief Access an array element by index.
  const JsonValue& operator[](size_t index) const { return Items()[index]; }

  bool IsNull() const { return type == JsonType::kNull; }
  size_t Size() const { return type == JsonType::kArray ? Items().size() : 0; }
  bool HasKey(const std::string& key) const { return IndexOfKey(key) != kNoKey; }

 private:
  static constexpr size_t kNoKey = static_cast<size_t>(-1);
  inline size_t IndexOfKey(const std::string& key) const;
};

/// Element and member storage of an array or object value. Object keys are
/// interned by the parser, so rows repeating the same names share one string.
struct JsonMembers {
  std::vector<JsonValue> items;
  std::vector<std::shared_ptr<const std::string>> keys;
};

inline const std::vector<JsonValue>& JsonValue::Items() const {
  static const std::vector<JsonValue> kEmpty;
  return members ? members->items : kEmpty;
}

inline const std::string& JsonValue::KeyAt(size_t index) const { return *members->keys[index]; }

inline size_t JsonValue::IndexOfKey(const std::string& key) const {
  if (!members) return kNoKey;
  for (size_t i = 0; i < members->keys.size(); ++i) {
    if (*members->keys[i] == key) return i;
  }
  return kNoKey;
}

/// @brief Render @p text for a diagnostic: control bytes become `\xNN` escapes.
inline std::string PrintableText(const std::string& text) {
  static const char kHex[] = "0123456789ABCDEF";
  std::string out;
  for (char c : text) {
    const unsigned char byte = static_cast<unsigned char>(c);
    if (byte < 0x20 || byte == 0x7F) {
      out += "\\x";
      out += kHex[byte >> 4];
      out += kHex[byte & 0x0F];
    } else {
      out += c;
    }
  }
  return out;
}

/// @brief Append one strict UTF-8 code point from a raw JSON string.
///
/// JSON text is Unicode; accepting malformed UTF-8 here would let the writer
/// later emit a syntactically invalid JSON success result.
inline bool AppendUtf8CodePoint(const std::string& input, size_t& pos, std::string& output,
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
  /// @brief Parse @p input in place; the string must outlive the parser.
  explicit JsonParser(const std::string& input) : input_(input), pos_(0) {}
  JsonParser(std::string&&) = delete;

  /// @brief Parse the input and return a JsonValue. Sets error on failure.
  ///
  /// A row array (the document itself, or the value of a top-level "tests"
  /// key) keeps at most kMaxTests + 1 rows: later rows are still checked for
  /// syntax and then dropped, so an over-limit suite is refused by the row-count
  /// check without the whole tree being resident first.
  JsonValue Parse() {
    SkipWhitespace();
    auto val = ParseValue(0, /*row_array=*/true);
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

  JsonValue ParseValue(size_t depth = 0, bool row_array = false) {
    if (depth > kMaxNestingDepth) {
      error_ = "JSON nesting depth exceeds " + std::to_string(kMaxNestingDepth);
      return {};
    }
    SkipWhitespace();
    char c = Peek();
    if (c == '"') return ParseString();
    if (c == '{') return ParseObject(depth + 1);
    if (c == '[') return ParseArray(depth + 1, row_array);
    if (c == 't' || c == 'f') return ParseBool();
    if (c == 'n') return ParseNull();
    if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber();
    if (pos_ >= input_.size()) {
      error_ = "unexpected end of input at position " + std::to_string(pos_);
    } else if (c >= 0x20 && c < 0x7F) {
      error_ = std::string("unexpected character '") + c + "' at position " + std::to_string(pos_);
    } else {
      static const char kHex[] = "0123456789ABCDEF";
      const unsigned char byte = static_cast<unsigned char>(c);
      error_ = std::string("unexpected byte 0x") + kHex[byte >> 4] + kHex[byte & 0x0F] +
               " at position " + std::to_string(pos_);
    }
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
    // Underflow and subnormal results set ERANGE but are finite, as in JS.
    val.number_val = std::strtod(token.c_str(), &end);
    if (end != token.c_str() + token.size() || !std::isfinite(val.number_val)) {
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

  JsonValue ParseArray(size_t depth, bool row_array) {
    if (!Expect('[')) return {};
    JsonValue val;
    val.type = JsonType::kArray;
    SkipWhitespace();
    if (Peek() == ']') {
      ++pos_;
      return val;
    }
    auto members = std::make_shared<JsonMembers>();
    while (true) {
      auto element = ParseValue(depth);
      if (!error_.empty()) return {};
      if (!row_array || members->items.size() <= coverwise::model::kMaxTests) {
        members->items.push_back(std::move(element));
      }
      SkipWhitespace();
      if (Peek() == ',') {
        ++pos_;
        continue;
      }
      break;
    }
    if (!Expect(']')) return {};
    members->items.shrink_to_fit();
    val.members = std::move(members);
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
    auto members = std::make_shared<JsonMembers>();
    std::unordered_set<std::string> seen_keys;
    while (true) {
      SkipWhitespace();
      auto key = ParseString();
      if (!error_.empty()) return {};
      if (!Expect(':')) return {};
      if (!seen_keys.insert(key.string_val).second) {
        error_ = "duplicate object key '" + PrintableText(key.string_val) + "'";
        return {};
      }
      if (seen_keys.size() > kMaxObjectMembers) {
        error_ = "object has too many members";
        return {};
      }
      const bool tests_member = depth == 1 && key.string_val == "tests";
      auto value = ParseValue(depth, tests_member);
      if (!error_.empty()) return {};
      members->keys.push_back(InternKey(key.string_val));
      members->items.push_back(std::move(value));
      SkipWhitespace();
      if (Peek() == ',') {
        ++pos_;
        continue;
      }
      break;
    }
    if (!Expect('}')) return {};
    members->keys.shrink_to_fit();
    members->items.shrink_to_fit();
    val.members = std::move(members);
    return val;
  }

  std::shared_ptr<const std::string> InternKey(const std::string& key) {
    auto& slot = key_pool_[key];
    if (!slot) slot = std::make_shared<const std::string>(key);
    return slot;
  }

  const std::string& input_;
  size_t pos_;
  std::string error_;
  std::unordered_map<std::string, std::shared_ptr<const std::string>> key_pool_;
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

}  // namespace cli
}  // namespace coverwise

#endif  // COVERWISE_TOOLS_JSON_H_
