/// @file string_util.cpp

#include "util/string_util.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

// libc++ implemented std::from_chars for floating point in LLVM 20, and Apple
// gates it behind a macOS 26 deployment target, so it is unusable on every
// currently supported macOS release. std::to_chars has been available since
// macOS 13.3 and has no equivalent fallback that is guaranteed to reproduce its
// shortest round-trip digits, so that one is a hard requirement instead.
#if defined(__cpp_lib_to_chars) &&                                   \
    (!defined(_LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT) || \
     _LIBCPP_AVAILABILITY_HAS_FROM_CHARS_FLOATING_POINT)
#define COVERWISE_HAS_FLOAT_FROM_CHARS 1
#else
#define COVERWISE_HAS_FLOAT_FROM_CHARS 0
#endif

#if defined(_LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT) && \
    !_LIBCPP_AVAILABILITY_HAS_TO_CHARS_FLOATING_POINT
#error "coverwise requires a macOS 13.3 or newer deployment target"
#endif

namespace coverwise {
namespace util {
namespace {

unsigned char FoldAscii(unsigned char value) {
  if (value >= static_cast<unsigned char>('A') && value <= static_cast<unsigned char>('Z')) {
    return static_cast<unsigned char>(value + ('a' - 'A'));
  }
  return value;
}

bool HasNegativeDecimalOrder(const std::string& value) {
  size_t i = (value[0] == '+' || value[0] == '-') ? 1 : 0;
  const size_t exponent_pos = value.find_first_of("eE", i);
  const size_t mantissa_end = exponent_pos == std::string::npos ? value.size() : exponent_pos;
  const size_t decimal_pos = value.find('.', i);
  const size_t integer_digits = decimal_pos == std::string::npos || decimal_pos >= mantissa_end
                                    ? mantissa_end - i
                                    : decimal_pos - i;

  size_t digit_position = 0;
  size_t first_nonzero = std::string::npos;
  for (size_t pos = i; pos < mantissa_end; ++pos) {
    if (value[pos] == '.') continue;
    if (first_nonzero == std::string::npos && value[pos] != '0') first_nonzero = digit_position;
    ++digit_position;
  }
  if (first_nonzero == std::string::npos) return false;

  long long explicit_exponent = 0;
  if (exponent_pos != std::string::npos) {
    size_t pos = exponent_pos + 1;
    bool negative = false;
    if (value[pos] == '+' || value[pos] == '-') {
      negative = value[pos] == '-';
      ++pos;
    }
    for (; pos < value.size() && explicit_exponent < 1000000; ++pos) {
      explicit_exponent = explicit_exponent * 10 + (value[pos] - '0');
    }
    if (negative) explicit_exponent = -explicit_exponent;
  }

  const long long decimal_order = explicit_exponent + static_cast<long long>(integer_digits) -
                                  static_cast<long long>(first_nonzero) - 1;
  return decimal_order < 0;
}

/// @brief Outcome of parsing a decimal field, mirroring what std::from_chars
///   reports back to this file.
struct DecimalParse {
  double value = 0.0;
  bool complete = false;      ///< The whole field was consumed and represented.
  bool out_of_range = false;  ///< Valid decimal syntax, not a representable double.
};

/// @brief Parse a decimal field independently of the active C locale.
/// @param begin First character of the field, past any leading '+'.
/// @param end One past the last character of the field.
DecimalParse ParseDecimal(const char* begin, const char* end) {
  DecimalParse parsed;
#if COVERWISE_HAS_FLOAT_FROM_CHARS
  const auto result = std::from_chars(begin, end, parsed.value, std::chars_format::general);
  parsed.complete = result.ec == std::errc{} && result.ptr == end;
  parsed.out_of_range = result.ec == std::errc::result_out_of_range;
#else
  // num_get reports malformed input and out-of-range values through the same
  // failbit, so the syntax check is what separates them.
  const std::string field(begin, end);
  if (!IsNumeric(field)) return parsed;
  std::istringstream stream(field);
  stream.imbue(std::locale::classic());
  stream >> parsed.value;
  parsed.complete = !stream.fail();
  parsed.out_of_range = stream.fail();
#endif
  return parsed;
}

}  // namespace

bool CaseInsensitiveEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (FoldAscii(static_cast<unsigned char>(a[i])) !=
        FoldAscii(static_cast<unsigned char>(b[i]))) {
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

double ToDouble(const std::string& s) {
  if (s.empty()) return std::numeric_limits<double>::quiet_NaN();
  const char* begin = s.data();
  const char* end = begin + s.size();
  if (*begin == '+') ++begin;

  const DecimalParse result = ParseDecimal(begin, end);
  if (result.complete) return result.value;

  // IsNumeric is a documented precondition. The only remaining expected
  // failure is a finite decimal outside double's representable range.
  if (result.out_of_range) {
    const bool negative = s[0] == '-';
    if (HasNegativeDecimalOrder(s)) {
      return negative ? -0.0 : 0.0;
    }
    return negative ? -std::numeric_limits<double>::infinity()
                    : std::numeric_limits<double>::infinity();
  }
  return std::numeric_limits<double>::quiet_NaN();
}

bool TryParseFiniteDouble(const std::string& s, double* out) {
  if (!IsNumeric(s)) return false;
  const char* begin = s.data();
  const char* end = begin + s.size();
  if (*begin == '+') ++begin;

  const DecimalParse result = ParseDecimal(begin, end);
  if (!result.complete || !std::isfinite(result.value)) return false;
  *out = result.value;
  return true;
}

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
