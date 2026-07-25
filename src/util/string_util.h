/// @file string_util.h
/// @brief General-purpose string conversion utilities.

#ifndef COVERWISE_UTIL_STRING_UTIL_H_
#define COVERWISE_UTIL_STRING_UTIL_H_

#include <string>

namespace coverwise {
namespace util {

/// @brief Compare two strings case-insensitively.
/// @param a First string.
/// @param b Second string.
/// @return true if the strings are equal ignoring case.
bool CaseInsensitiveEqual(const std::string& a, const std::string& b);

/// @brief Check if a string can be parsed as a double.
/// @param s The string to check.
/// @return true if s is a valid double representation.
bool IsNumeric(const std::string& s);

/// @brief Parse a string as a double. Undefined behavior if !IsNumeric(s).
/// @param s The string to parse.
/// @return The parsed double value.
double ToDouble(const std::string& s);

/// @brief Parse a string that must denote a finite double.
///
/// Unlike ToDouble this validates its input, so it is the entry point for
/// callers that accept arbitrary text and reject anything unrepresentable.
///
/// @param s The string to parse.
/// @param out Receives the parsed value on success; untouched otherwise.
/// @return true if s is numeric and denotes a finite double.
bool TryParseFiniteDouble(const std::string& s, double* out);

/// @brief Format a double exactly like JavaScript's Number.prototype.toString().
///
/// Produces the shortest decimal string that round-trips to the same double,
/// following the ECMAScript Number-to-String algorithm (ECMA-262 6.1.6.1.20).
/// This guarantees byte-identical output to the JS surfaces (which use
/// String(value)), so a numeric value renders the same string on every
/// surface (WASM, pure-JS, CLI, boundary expansion).
///
/// Examples: 3.14 -> "3.14", 1.0/3.0 -> "0.3333333333333333", -0.0 -> "0",
/// 100.0 -> "100", 1e-7 -> "1e-7", 1e21 -> "1e+21".
///
/// @param value A finite double (NaN/Infinity are not expected as parameter
///   values and are formatted via std::to_chars as a fallback).
/// @return The canonical JS string representation.
std::string JsNumberToString(double value);

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_STRING_UTIL_H_
