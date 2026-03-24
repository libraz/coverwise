/// @file string_util.h
/// @brief General-purpose string conversion utilities.

#ifndef COVERWISE_UTIL_STRING_UTIL_H_
#define COVERWISE_UTIL_STRING_UTIL_H_

#include <string>

namespace coverwise {
namespace util {

/// @brief Check if a string can be parsed as a double.
/// @param s The string to check.
/// @return true if s is a valid double representation.
bool IsNumeric(const std::string& s);

/// @brief Parse a string as a double. Undefined behavior if !IsNumeric(s).
/// @param s The string to parse.
/// @return The parsed double value.
double ToDouble(const std::string& s);

}  // namespace util
}  // namespace coverwise

#endif  // COVERWISE_UTIL_STRING_UTIL_H_
