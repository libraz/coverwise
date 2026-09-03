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
/// Three rules hold across every accessor:
///   - `undefined` and `null` are absence. An optional field holding either is
///     reported as missing, so the documented default applies; a required field
///     holding either is rejected exactly as if the key were not there.
///   - Key presence never licenses a dereference. HasField answers whether a
///     key exists and nothing else; the value still has to come through an
///     accessor that establishes its shape.
///   - A read runs the caller's JavaScript, and a read that throws is a
///     rejection like any malformed shape. A getter, a Proxy trap or a computed
///     member of a class instance may throw, and what it throws is a JavaScript
///     exception rather than a C++ one — so nothing here reads a caller-supplied
///     object from C++, and the throw never becomes an unwind through the
///     WebAssembly frames. See the guarded reads in `detail`.
///
/// Each shape comes in two forms. `Require`/`Optional` take the rejection text
/// and are what a per-field parser wants; `Try` returns an empty optional and
/// leaves the text to the caller, which is what a loop over cells wants, since
/// composing a message it will not use costs more than the check itself.

#ifndef COVERWISE_BINDING_WASM_JS_VALUE_H_
#define COVERWISE_BINDING_WASM_JS_VALUE_H_

#ifdef __EMSCRIPTEN__

#include <emscripten/em_js.h>
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

/// The JS library symbols the reads below name, declared beside the code that
/// names them rather than at the link line.
EM_JS_DEPS(coverwise_js_value, "$Emval,$UTF8ToString");

// Every read of a caller-supplied object happens inside JavaScript, in the
// functions below, so that caller code which throws is contained where it runs.
//
// Such a read executes whatever the caller attached to the property: a getter,
// a Proxy trap, a computed member of a class instance. What that code throws is
// a JavaScript exception, not a C++ one, so the `catch (const std::exception&)`
// at the export boundary never sees it. It unwinds out through the WebAssembly
// frames instead — past every destructor on the stack, so the handles and
// buffers the call was holding are never released — and reaches the caller as a
// foreign throw from a function documented to return an error object. Reactive
// stores and class instances with computed properties are ordinary input, so
// this is a shape the binding has to survive rather than an exotic one.
//
// Each read answers with a value or with a null handle. Null is unambiguous:
// emval reserves handle 0 and never hands it out, and a property that genuinely
// holds `undefined` comes back as the handle for `undefined`.
//
// A read that failed also hands back what the caller threw, through the
// `thrown` out-parameter, so the rejection can quote it the way the wrapper
// package quotes a throw it caught itself. The value travels as a handle and is
// described by coverwise_js_describe below: describing it is caller code again,
// and doing that here — inside JavaScript, inside a catch — is what keeps the
// reporting path from reopening the hole the read just closed.
//
// What is deliberately NOT routed through these, and why, since the alternative
// reading is that the guards were applied unevenly. A guard is needed where a
// read can reach caller code; the following cannot, and guarding them would add
// a crossing per cell to buy nothing:
//   - JsScalar's accessors — RequireString, RequireNumber, ToText — run against
//     a value whose kind was established when the scalar was made. There is no
//     getter, trap or valueOf left to reach: `String(n)` of a number the runtime
//     already classified is the runtime's own conversion.
//   - JsValue's classifiers — TryScalar's isString/isNumber/isTrue/isFalse,
//     IsNullish, and the typeOf in TryObject — are `typeof` tests and handle
//     comparisons. A Proxy has no trap for either.
//   - The key list in ForEachEntry and FieldCount is read directly for its
//     length and its elements, because it is the array Object.keys returned to
//     this layer a moment earlier rather than anything the caller supplied.
// Whatever comes back out of a read, on the other hand, is the caller's again,
// and reaches its next question through these same guards.

/// @brief `object[name]`, or a null handle if reading it threw.
EM_JS(emscripten::EM_VAL, coverwise_js_read_name,
      (emscripten::EM_VAL object, const char* name, emscripten::EM_VAL* thrown), {
        try {
          return Emval.toHandle(Emval.toValue(object)[UTF8ToString(name)]);
        } catch (e) {
          HEAPU32[thrown >> 2] = Emval.toHandle(e);
          return 0;
        }
      });

/// @brief `object[key]` for a key that is already a JS value, or a null handle
///        if reading it threw.
EM_JS(emscripten::EM_VAL, coverwise_js_read_key,
      (emscripten::EM_VAL object, emscripten::EM_VAL key, emscripten::EM_VAL* thrown), {
        try {
          return Emval.toHandle(Emval.toValue(object)[Emval.toValue(key)]);
        } catch (e) {
          HEAPU32[thrown >> 2] = Emval.toHandle(e);
          return 0;
        }
      });

/// @brief `array[index]`, or a null handle if reading it threw.
EM_JS(emscripten::EM_VAL, coverwise_js_read_index,
      (emscripten::EM_VAL array, uint32_t index, emscripten::EM_VAL* thrown), {
        try {
          return Emval.toHandle(Emval.toValue(array)[index]);
        } catch (e) {
          HEAPU32[thrown >> 2] = Emval.toHandle(e);
          return 0;
        }
      });

/// @brief `Object.keys(object)` through the caller's resolved constructor, or a
///        null handle if listing them threw.
EM_JS(emscripten::EM_VAL, coverwise_js_own_keys,
      (emscripten::EM_VAL object_ctor, emscripten::EM_VAL object, emscripten::EM_VAL* thrown), {
        try {
          return Emval.toHandle(Emval.toValue(object_ctor).keys(Emval.toValue(object)));
        } catch (e) {
          HEAPU32[thrown >> 2] = Emval.toHandle(e);
          return 0;
        }
      });

/// @brief Whether @p object carries @p name as an own key: 1 yes, 0 no, -1 if
///        asking threw.
///
/// `val::hasOwnProperty` walks `Object.prototype.hasOwnProperty` and calls it,
/// which is four crossings for one question and leaves a Proxy's
/// `getOwnPropertyDescriptor` trap free to throw across them.
EM_JS(int, coverwise_js_has_own,
      (emscripten::EM_VAL object, const char* name, emscripten::EM_VAL* thrown), {
        try {
          return Object.prototype.hasOwnProperty.call(Emval.toValue(object), UTF8ToString(name))
                     ? 1
                     : 0;
        } catch (e) {
          HEAPU32[thrown >> 2] = Emval.toHandle(e);
          return -1;
        }
      });

/// @brief The text describing a value the caller threw.
///
/// A thrown value answers questions with caller code as readily as the property
/// that produced it: `String(e)` reaches a `toString`, and a Proxy can throw
/// from `get` and from `getPrototypeOf` alike. Asking here keeps that inside
/// JavaScript, and a value that refuses every question is named as one rather
/// than becoming a second escape out of the reporting path.
///
/// An Error is quoted by its message alone, matching what the wrapper package
/// reports for a throw it caught before the module was reached, so one input
/// produces one text whichever surface read it.
EM_JS(emscripten::EM_VAL, coverwise_js_describe, (emscripten::EM_VAL thrown), {
  try {
    const value = Emval.toValue(thrown);
    // Compared by length rather than against the empty string: the C++
    // preprocessor carries this body through as a string, and the formatter
    // that reflows it reads `!` `==` as two C++ tokens, which would split a
    // strict inequality in half.
    const said = value instanceof Error ? value.message : "";
    return Emval.toHandle(said.length > 0 ? said : String(value));
  } catch (e) {
    return Emval.toHandle("a value that cannot be described");
  }
});

/// @brief Whether a JS value is a genuine Array.
///
/// A string is indexable and carries a `length`, so a caller who forgot to wrap
/// a value in an array would otherwise be read character by character.
///
/// `Array.isArray` is resolved once and kept on the function object. Resolving
/// it against the JS global on every call is a cost each row and each declared
/// value would pay, which is the same reason the constructors the binding
/// passes in are resolved once per parse rather than per cell. A revoked Proxy
/// makes even this question throw; such a value is no Array, and whatever is
/// read from it next is rejected by the read that reads it.
EM_JS(bool, coverwise_js_is_array, (emscripten::EM_VAL value), {
  // Assigned rather than coalesced: `?` `?` `=` is a trigraph to the C++
  // preprocessor that carries this body through as a string.
  const isArray = coverwise_js_is_array.isArray || (coverwise_js_is_array.isArray = Array.isArray);
  try {
    return isArray(Emval.toValue(value)) ? 1 : 0;
  } catch (e) {
    return 0;
  }
});

[[noreturn]] inline void Reject(const std::string& message) { throw std::runtime_error(message); }

/// @brief Reject a read that ran caller code and threw.
///
/// One wording for the whole family, so the surface says the same thing whether
/// the read that threw was a named field, an element or a key listing, and the
/// same thing the wrapper package says for a throw it caught before the module
/// was reached. @p what names the field; the parenthesis carries the caller's
/// own text, which is the part that says why.
[[noreturn]] inline void RejectThrew(const std::string& what, emscripten::EM_VAL thrown) {
  std::string described = "a value that cannot be described";
  if (thrown != nullptr) {
    const emscripten::val value = emscripten::val::take_ownership(thrown);
    described =
        emscripten::val::take_ownership(coverwise_js_describe(value.as_handle())).as<std::string>();
  }
  Reject("Invalid input: reading " + what + " threw (" + described + ").");
}

inline bool IsArrayValue(const emscripten::val& value) {
  return coverwise_js_is_array(value.as_handle());
}

/// @brief `object[name]`, rejecting if the read threw.
inline emscripten::val ReadName(const emscripten::val& object, const char* name) {
  emscripten::EM_VAL thrown = nullptr;
  const emscripten::EM_VAL read = coverwise_js_read_name(object.as_handle(), name, &thrown);
  if (read == nullptr) RejectThrew(name, thrown);
  return emscripten::val::take_ownership(read);
}

/// @brief `object[key]`, rejecting if the read threw. @p name describes @p key.
inline emscripten::val ReadKey(const emscripten::val& object, const emscripten::val& key,
                               const std::string& name) {
  emscripten::EM_VAL thrown = nullptr;
  const emscripten::EM_VAL read =
      coverwise_js_read_key(object.as_handle(), key.as_handle(), &thrown);
  if (read == nullptr) RejectThrew(name, thrown);
  return emscripten::val::take_ownership(read);
}

/// @brief `array[index]`, rejecting if the read threw.
inline emscripten::val ReadIndex(const emscripten::val& array, uint32_t index) {
  emscripten::EM_VAL thrown = nullptr;
  const emscripten::EM_VAL read = coverwise_js_read_index(array.as_handle(), index, &thrown);
  if (read == nullptr) RejectThrew("index " + std::to_string(index) + " of an array", thrown);
  return emscripten::val::take_ownership(read);
}

/// @brief The own enumerable keys of @p object, rejecting if listing them threw.
inline emscripten::val ReadOwnKeys(const emscripten::val& object_ctor,
                                   const emscripten::val& object) {
  emscripten::EM_VAL thrown = nullptr;
  const emscripten::EM_VAL keys =
      coverwise_js_own_keys(object_ctor.as_handle(), object.as_handle(), &thrown);
  if (keys == nullptr) RejectThrew("the keys of an object", thrown);
  return emscripten::val::take_ownership(keys);
}

/// @brief Whether @p object carries @p name as an own key, rejecting if asking
///        threw.
inline bool HasOwnName(const emscripten::val& object, const char* name) {
  emscripten::EM_VAL thrown = nullptr;
  const int present = coverwise_js_has_own(object.as_handle(), name, &thrown);
  if (present < 0) RejectThrew(name, thrown);
  return present != 0;
}

/// @brief The `length` of an array, rejecting if reading it threw or produced
///        something that is not a count.
///
/// The number is classified before it is taken: `val::as<uint32_t>()` runs the
/// caller's `valueOf` on anything that is not already a number, which is the
/// same hazard as the read that produced it. A genuine Array always answers
/// with a count; a Proxy standing in for one need not.
inline uint32_t ReadLength(const emscripten::val& array) {
  const emscripten::val length = ReadName(array, "length");
  const double count = length.isNumber() ? length.as<double>() : -1.0;
  const uint32_t size = (count >= 0.0 && count <= 4294967295.0) ? static_cast<uint32_t>(count) : 0u;
  // Round-tripping rejects a negative, a fraction and a NaN in one comparison.
  if (static_cast<double>(size) != count) {
    Reject("Invalid input: an array reported a length that is not a count.");
  }
  return size;
}

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
  /// Every JS number qualifies, NaN and the infinities included. This layer
  /// decides shape: whether the caller supplied a number at all, as opposed to
  /// an object or a string. Which numbers a particular field may hold is a
  /// different question, and it belongs to the layer that owns the field, so
  /// that one wording answers it for every surface.
  ///
  /// Moving finiteness in here would look like a tightening and would in fact
  /// remove behaviour. A non-finite boundary endpoint or step would be turned
  /// away as a malformed shape, and the rules that judge it — the ones that say
  /// a range must be finite and ordered, and that an expansion must produce
  /// finite values — would become unreachable from this surface. The pure
  /// TypeScript surface would still reach them, so the same input would be
  /// refused by both with two different explanations, which is exactly what the
  /// cross-surface acceptance tests exist to prevent.
  ///
  /// What this does not describe is a model anyone can write. JSON has no
  /// literal for NaN or for either infinity, so a model holding one has no JSON
  /// form, and every surface fed JSON refuses it before the question reaches
  /// here: the native CLI as a parse error, and the npm wrapper, its pure
  /// TypeScript entry point and the Python package — which wraps the CLI
  /// binary — as invalid input. A non-finite number arrives only from an
  /// embedder calling the compiled module with JavaScript values in hand. The
  /// permissiveness above is what that one caller meets, not a position on what
  /// coverwise accepts, and it is not a reason to relax the surfaces that
  /// refuse: doing so would let a model be built through one of them that could
  /// not be handed to the others at all.
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
  uint32_t size() const { return detail::ReadLength(value_); }

  /// @brief The element at @p index, whose own shape is still unestablished.
  JsValue At(uint32_t index) const { return JsValue(detail::ReadIndex(value_, index)); }

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
  bool HasField(const char* field) const { return detail::HasOwnName(value_, field); }

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
    // The key list comes from Object.keys, so it is an array this layer made
    // and its own length and elements are read directly.
    const emscripten::val keys = detail::ReadOwnKeys(object_ctor, value_);
    const uint32_t count = keys["length"].as<uint32_t>();
    for (uint32_t i = 0; i < count; ++i) {
      const emscripten::val key = keys[i];
      std::string name = key.as<std::string>();
      emscripten::val value = detail::ReadKey(value_, key, name);
      visit(std::move(name), JsValue(std::move(value)));
    }
  }

  /// @brief Number of own enumerable keys.
  uint32_t FieldCount(const emscripten::val& object_ctor) const {
    return detail::ReadOwnKeys(object_ctor, value_)["length"].as<uint32_t>();
  }

 private:
  friend class JsValue;
  explicit JsObject(emscripten::val value) : value_(std::move(value)) {}

  JsValue Field(const char* field) const { return JsValue(detail::ReadName(value_, field)); }

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
