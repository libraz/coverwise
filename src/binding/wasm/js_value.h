/// @file js_value.h
/// @brief Shape-checked access to untrusted JavaScript input.
///
/// Every field the binding reads out of a caller-supplied object comes from an
/// untrusted source: an embedder calling the compiled module directly gets none
/// of the checks the JS wrapper runs first. Reading such a field with
/// `val::operator[]` establishes nothing — the result may be `undefined`, a
/// string where an array was meant, or an object with no `value` member — and
/// every use of it then has to remember to check for itself. A guard that has
/// to be remembered is a guard that gets forgotten one field at a time.
///
/// The wrappers here take that decision away from the caller. A JsObject has no
/// subscript operator, so a field can only be reached through an accessor that
/// states the shape it expects, and the result is either a value of that shape
/// or a std::runtime_error, which the exported functions turn into an
/// INVALID_INPUT result. Reaching a raw `emscripten::val` field is not a
/// convention to follow but a compile error.
///
/// Two rules hold across every accessor:
///   - `undefined` and `null` are absence. An optional field holding either is
///     reported as missing, so the documented default applies; a required field
///     holding either is rejected exactly as if the key were not there.
///   - Key presence never licenses a dereference. HasField answers whether a
///     key exists and nothing else; the value still has to come through an
///     accessor that establishes its shape.
///
/// Each shape comes in two forms. `Require`/`Optional` take the rejection text
/// and are what a per-field parser wants; `Try` returns an empty optional and
/// leaves the text to the caller, which is what a loop over cells wants, since
/// composing a message it will not use costs more than the check itself.

#ifndef COVERWISE_BINDING_WASM_JS_VALUE_H_
#define COVERWISE_BINDING_WASM_JS_VALUE_H_

#ifdef __EMSCRIPTEN__

#include <emscripten/val.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace coverwise {
namespace binding {

class JsArray;
class JsObject;
class JsScalar;

namespace detail {

/// @brief Whether a JS value is a genuine Array.
///
/// A string is indexable and carries a `length`, so a caller who forgot to wrap
/// a value in an array would otherwise be read character by character.
inline bool IsArrayValue(const emscripten::val& value) {
  return emscripten::val::global("Array").call<bool>("isArray", value);
}

[[noreturn]] inline void Reject(const std::string& message) { throw std::runtime_error(message); }

}  // namespace detail

/// @brief A value whose shape has not been established yet.
///
/// The only thing that can be done with one is to ask for a shape. Every
/// accessor below hands back a JsValue for anything it has not classified.
class JsValue {
 public:
  explicit JsValue(emscripten::val value) : value_(std::move(value)) {}

  /// @brief Whether the value is `undefined` or `null`, which mean the same
  ///        thing here: the caller did not supply this field.
  bool IsNullish() const { return value_.isUndefined() || value_.isNull(); }

  /// @brief The value as a scalar — a string, a boolean, or a number — or
  ///        nothing if it is any other shape.
  ///
  /// Every JS number qualifies, NaN and the infinities included, and that is a
  /// rule rather than an oversight. This layer decides shape: whether the
  /// caller supplied a number at all, as opposed to an object or a string.
  /// Which numbers a particular field may hold is a different question, and it
  /// belongs to the layer that owns the field, so that one wording answers it
  /// for every surface.
  ///
  /// Moving finiteness in here would look like a tightening and would in fact
  /// remove behaviour. A non-finite boundary endpoint or step would be turned
  /// away as a malformed shape, and the rules that judge it — the ones that say
  /// a range must be finite and ordered, and that an expansion must produce
  /// finite values — would become unreachable from this surface. The pure
  /// TypeScript surface would still reach them, so the same input would be
  /// refused by both with two different explanations, which is exactly what the
  /// cross-surface acceptance tests exist to prevent.
  std::optional<JsScalar> TryScalar() const;

  /// @brief The value as an Array, or nothing if it is any other shape.
  std::optional<JsArray> TryArray() const;

  /// @brief The value as a plain object, or nothing if it is any other shape.
  ///
  /// Arrays, `null` and functions are other shapes: a caller who supplied one
  /// where an object belongs described something the engine cannot read.
  std::optional<JsObject> TryObject() const;

  JsScalar RequireScalar(const std::string& message) const;
  JsArray RequireArray(const std::string& message) const;
  JsObject RequireObject(const std::string& message) const;

  std::optional<JsScalar> OptionalScalar(const std::string& message) const;
  std::optional<JsArray> OptionalArray(const std::string& message) const;
  std::optional<JsObject> OptionalObject(const std::string& message) const;

 private:
  emscripten::val value_;
};

/// @brief A value already known to be a string, a boolean, or a number.
///
/// Which of the three it is was settled when the scalar was made, and is
/// remembered rather than asked again. Every `isString`-style predicate on a
/// `val` is a call out of WebAssembly into JavaScript, and a row cell would
/// otherwise pay for the same question twice — once to classify it and once to
/// render it. That is a per-cell cost on the hottest path the binding has.
class JsScalar {
 public:
  bool IsString() const { return kind_ == Kind::kString; }
  bool IsNumber() const { return kind_ == Kind::kNumber; }
  bool IsBool() const { return kind_ == Kind::kTrue || kind_ == Kind::kFalse; }

  std::string RequireString(const std::string& message) const {
    if (!IsString()) detail::Reject(message);
    return value_.as<std::string>();
  }

  double RequireNumber(const std::string& message) const {
    if (!IsNumber()) detail::Reject(message);
    return value_.as<double>();
  }

  bool RequireBool(const std::string& message) const {
    if (!IsBool()) detail::Reject(message);
    return kind_ == Kind::kTrue;
  }

  /// @brief Render the scalar the way JavaScript would.
  ///
  /// - string  → as-is
  /// - boolean → "true" / "false"
  /// - number  → JS `String(n)` ("42", "3.14", "1e-7", "0" for -0)
  ///
  /// Numbers go through the JS runtime, so the text is byte-identical to the
  /// pure-TypeScript surface by construction rather than by a second
  /// implementation of the Number-to-String algorithm.
  ///
  /// @param string_ctor The JS `String` constructor. `val::global` resolves a
  ///        name against the JS global object on every call, so a conversion
  ///        loop resolves it once and passes it in rather than paying that
  ///        lookup per value.
  std::string ToText(const emscripten::val& string_ctor) const {
    switch (kind_) {
      case Kind::kString:
        return value_.as<std::string>();
      case Kind::kTrue:
        return "true";
      case Kind::kFalse:
        return "false";
      case Kind::kNumber:
        break;
    }
    return string_ctor.call<std::string>("call", emscripten::val::null(), value_);
  }

 private:
  friend class JsValue;

  enum class Kind { kString, kNumber, kTrue, kFalse };

  JsScalar(emscripten::val value, Kind kind) : value_(std::move(value)), kind_(kind) {}

  emscripten::val value_;
  Kind kind_;
};

/// @brief A value already known to be an Array.
class JsArray {
 public:
  uint32_t size() const { return value_["length"].as<uint32_t>(); }

  /// @brief The element at @p index, whose own shape is still unestablished.
  JsValue At(uint32_t index) const { return JsValue(value_[index]); }

  /// @brief The element at @p index as a non-empty string.
  std::string RequireNonEmptyStringAt(uint32_t index, const std::string& message) const {
    const std::string text = At(index).RequireScalar(message).RequireString(message);
    if (text.empty()) detail::Reject(message);
    return text;
  }

 private:
  friend class JsValue;
  explicit JsArray(emscripten::val value) : value_(std::move(value)) {}

  emscripten::val value_;
};

/// @brief A value already known to be a plain object.
class JsObject {
 public:
  /// @brief Whether the object carries @p field as an own key.
  ///
  /// Presence only. It says nothing about the value, which still has to be read
  /// through one of the accessors below — a key present with an `undefined`
  /// value is not a readable field.
  bool HasField(const char* field) const { return value_.hasOwnProperty(field); }

  JsScalar RequireScalar(const char* field, const std::string& message = {}) const {
    return Field(field).RequireScalar(Or(message, ScalarText(field)));
  }
  JsArray RequireArray(const char* field, const std::string& message = {}) const {
    return Field(field).RequireArray(Or(message, ArrayText(field)));
  }
  JsObject RequireObject(const char* field, const std::string& message = {}) const {
    return Field(field).RequireObject(Or(message, ObjectText(field)));
  }

  std::optional<JsScalar> OptionalScalar(const char* field, const std::string& message = {}) const {
    return Field(field).OptionalScalar(Or(message, ScalarText(field)));
  }
  std::optional<JsArray> OptionalArray(const char* field, const std::string& message = {}) const {
    return Field(field).OptionalArray(Or(message, ArrayText(field)));
  }
  std::optional<JsObject> OptionalObject(const char* field, const std::string& message = {}) const {
    return Field(field).OptionalObject(Or(message, ObjectText(field)));
  }

  /// @brief Visit every own enumerable entry as (key, value).
  ///
  /// Object.keys is used rather than hasOwnProperty(const char*), which can
  /// fail on non-ASCII (UTF-8) property names in Emscripten. The key is indexed
  /// back into the object as the JS value it already is, so an entry costs one
  /// crossing for the name instead of one in each direction.
  ///
  /// @param object_ctor The JS `Object` constructor, resolved once by the
  ///        caller for the same reason JsScalar::ToText takes `String`.
  template <typename Fn>
  void ForEachEntry(const emscripten::val& object_ctor, Fn&& visit) const {
    const emscripten::val keys = object_ctor.call<emscripten::val>("keys", value_);
    const uint32_t count = keys["length"].as<uint32_t>();
    for (uint32_t i = 0; i < count; ++i) {
      const emscripten::val key = keys[i];
      visit(key.as<std::string>(), JsValue(value_[key]));
    }
  }

  /// @brief Number of own enumerable keys.
  uint32_t FieldCount(const emscripten::val& object_ctor) const {
    return object_ctor.call<emscripten::val>("keys", value_)["length"].as<uint32_t>();
  }

 private:
  friend class JsValue;
  explicit JsObject(emscripten::val value) : value_(std::move(value)) {}

  JsValue Field(const char* field) const { return JsValue(value_[field]); }

  static const std::string& Or(const std::string& message, const std::string& fallback) {
    return message.empty() ? fallback : message;
  }
  static std::string ScalarText(const char* field) {
    return std::string("Invalid ") + field + ": expected string, number, or boolean.";
  }
  static std::string ArrayText(const char* field) {
    return std::string("Invalid ") + field + ": must be an array.";
  }
  static std::string ObjectText(const char* field) {
    return std::string("Invalid ") + field + ": must be an object.";
  }

  emscripten::val value_;
};

inline std::optional<JsScalar> JsValue::TryScalar() const {
  // Ordered by what a row cell usually is, since each test is a call into
  // JavaScript and the first match ends the sequence.
  if (value_.isString()) return JsScalar(value_, JsScalar::Kind::kString);
  if (value_.isNumber()) return JsScalar(value_, JsScalar::Kind::kNumber);
  if (value_.isTrue()) return JsScalar(value_, JsScalar::Kind::kTrue);
  if (value_.isFalse()) return JsScalar(value_, JsScalar::Kind::kFalse);
  return std::nullopt;
}

inline std::optional<JsArray> JsValue::TryArray() const {
  if (!detail::IsArrayValue(value_)) return std::nullopt;
  return JsArray(value_);
}

inline std::optional<JsObject> JsValue::TryObject() const {
  if (value_.isNull() || detail::IsArrayValue(value_)) return std::nullopt;
  if (value_.typeOf().as<std::string>() != "object") return std::nullopt;
  return JsObject(value_);
}

inline JsScalar JsValue::RequireScalar(const std::string& message) const {
  auto scalar = TryScalar();
  if (!scalar) detail::Reject(message);
  return *scalar;
}

inline JsArray JsValue::RequireArray(const std::string& message) const {
  auto array = TryArray();
  if (!array) detail::Reject(message);
  return *array;
}

inline JsObject JsValue::RequireObject(const std::string& message) const {
  auto object = TryObject();
  if (!object) detail::Reject(message);
  return *object;
}

inline std::optional<JsScalar> JsValue::OptionalScalar(const std::string& message) const {
  if (IsNullish()) return std::nullopt;
  return RequireScalar(message);
}

inline std::optional<JsArray> JsValue::OptionalArray(const std::string& message) const {
  if (IsNullish()) return std::nullopt;
  return RequireArray(message);
}

inline std::optional<JsObject> JsValue::OptionalObject(const std::string& message) const {
  if (IsNullish()) return std::nullopt;
  return RequireObject(message);
}

}  // namespace binding
}  // namespace coverwise

#endif  // __EMSCRIPTEN__

#endif  // COVERWISE_BINDING_WASM_JS_VALUE_H_
