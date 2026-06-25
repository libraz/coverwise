/// @file string_util.cpp

#include "util/string_util.h"

#include <cctype>
#include <cstdlib>

namespace coverwise {
namespace util {

bool CaseInsensitiveEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool IsNumeric(const std::string& s) {
  // Strict decimal grammar, identical to the TypeScript port:
  //   ^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$
  // Rejects "inf"/"nan" (any case), hex, whitespace, empty, thousands
  // separators, and multiple dots. No reliance on locale-dependent strtod.
  if (s.empty()) return false;
  size_t i = 0;
  const size_t len = s.size();

  // Optional leading sign.
  if (s[i] == '+' || s[i] == '-') ++i;

  size_t int_digits = 0;
  while (i < len && s[i] >= '0' && s[i] <= '9') {
    ++int_digits;
    ++i;
  }

  size_t frac_digits = 0;
  if (i < len && s[i] == '.') {
    ++i;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
      ++frac_digits;
      ++i;
    }
  }

  // Require at least one digit in the integer or fractional part.
  if (int_digits == 0 && frac_digits == 0) return false;

  // Optional exponent.
  if (i < len && (s[i] == 'e' || s[i] == 'E')) {
    ++i;
    if (i < len && (s[i] == '+' || s[i] == '-')) ++i;
    size_t exp_digits = 0;
    while (i < len && s[i] >= '0' && s[i] <= '9') {
      ++exp_digits;
      ++i;
    }
    if (exp_digits == 0) return false;
  }

  return i == len;
}

double ToDouble(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

}  // namespace util
}  // namespace coverwise
