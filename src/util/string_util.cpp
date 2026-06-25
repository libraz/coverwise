/// @file string_util.cpp

#include "util/string_util.h"

#include <cctype>
#include <charconv>
#include <cmath>
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

std::string JsNumberToString(double value) {
  // Non-finite values are not expected as parameter values; fall back to a
  // plain to_chars rendering so the function never throws.
  if (!std::isfinite(value)) {
    char buf[32];
    auto res = std::to_chars(buf, buf + sizeof(buf), value);
    return std::string(buf, res.ptr);
  }
  // JS String(-0) === "0" and String(0) === "0".
  if (value == 0.0) {
    return "0";
  }

  // Obtain the shortest round-trip digits via scientific notation, which
  // normalizes the result to "D[.DDD]e±XX". From this we recover the
  // ECMAScript (s, k, n) triple: `s` is the digit string (k digits, no sign),
  // and the value equals s x 10^(n-k).
  char buf[40];
  auto res = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::scientific);
  std::string sci(buf, res.ptr);

  bool negative = false;
  size_t i = 0;
  if (sci[i] == '-') {
    negative = true;
    ++i;
  }

  // Collect significant digits (skip the decimal point).
  std::string digits;
  for (; i < sci.size() && sci[i] != 'e' && sci[i] != 'E'; ++i) {
    if (sci[i] != '.') {
      digits.push_back(sci[i]);
    }
  }
  // Parse the base-10 exponent of the leading digit.
  int exp10 = 0;
  if (i < sci.size()) {
    ++i;  // skip 'e'
    exp10 = std::stoi(sci.substr(i));
  }

  const int k = static_cast<int>(digits.size());  // number of significant digits
  const int n = exp10 + 1;                        // position of the decimal point

  std::string out;
  if (k <= n && n <= 21) {
    // Integer with trailing zeros: digits followed by (n - k) zeros.
    out = digits;
    out.append(static_cast<size_t>(n - k), '0');
  } else if (0 < n && n <= 21) {
    // Decimal point inside the digit run.
    out = digits.substr(0, static_cast<size_t>(n)) + "." + digits.substr(static_cast<size_t>(n));
  } else if (-6 < n && n <= 0) {
    // Leading "0." followed by (-n) zeros, then the digits.
    out = "0.";
    out.append(static_cast<size_t>(-n), '0');
    out += digits;
  } else {
    // Exponential form: first digit, optional ".rest", then "e±(n-1)".
    out = digits.substr(0, 1);
    if (k > 1) {
      out += "." + digits.substr(1);
    }
    const int e = n - 1;
    out += 'e';
    out += (e >= 0) ? '+' : '-';
    out += std::to_string(std::abs(e));
  }

  return negative ? "-" + out : out;
}

}  // namespace util
}  // namespace coverwise
