#include "util/string_util.h"

#include <gtest/gtest.h>

#include <clocale>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using coverwise::util::CaseInsensitiveEqual;
using coverwise::util::IsNumeric;
using coverwise::util::JsNumberToString;
using coverwise::util::ToDouble;
using coverwise::util::TryParseFiniteDouble;

namespace {

constexpr bool kAcceptLiteral = true;
constexpr bool kRejectLiteral = false;

/// @brief One decimal and the double every surface must parse it into.
struct NumericParseCase {
  const char* text;
  uint64_t bits;  ///< IEEE-754 bit pattern of JavaScript's Number(text).
  bool accepts_literal;
};

// The corpus is shared with the TypeScript tests, which read the same file, so
// the decimals and their expected doubles exist only once for all surfaces.
const NumericParseCase kNumericParseCases[] = {
#define COVERWISE_NUMERIC_CASE(text, bits, disposition) {text, bits, disposition},
#include "numeric_parse_corpus.inc"
#undef COVERWISE_NUMERIC_CASE
};

uint64_t DoubleToBits(double value) {
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

// Byte-equality corpus for JsNumberToString. Every expected string is exactly
// what JavaScript String(value) / Number.prototype.toString() produces, so the
// C++ and TS surfaces render numeric values identically. The companion TS test
// in src/ts/util/string_util.test.ts asserts the same pairs.
TEST(StringUtilTest, JsNumberToStringMatchesJavaScript) {
  const std::vector<std::pair<double, std::string>> corpus = {
      {3.14, "3.14"},
      {0.1, "0.1"},
      {1.0 / 3.0, "0.3333333333333333"},
      {0.1 + 0.2, "0.30000000000000004"},
      {2.5, "2.5"},
      {-0.0, "0"},
      {0.0, "0"},
      {100.0, "100"},
      {1e-7, "1e-7"},
      {42.0, "42"},
      {-42.0, "-42"},
      {1e21, "1e+21"},
      {1e-21, "1e-21"},
      {1e-6, "0.000001"},
      {1e20, "100000000000000000000"},
      {-3.14, "-3.14"},
      {123456789.0, "123456789"},
      // Subnormal and extreme magnitudes.
      {5e-324, "5e-324"},                                    // Number.MIN_VALUE (subnormal)
      {2.2250738585072014e-308, "2.2250738585072014e-308"},  // smallest normal
      {1.7976931348623157e308, "1.7976931348623157e+308"},   // Number.MAX_VALUE
      // Safe-integer boundary.
      {9007199254740991.0, "9007199254740991"},  // MAX_SAFE_INTEGER
      {9007199254740992.0, "9007199254740992"},
      // Exponential thresholds (n == 22 and n == -7 tip into scientific form).
      {1e22, "1e+22"},
      {5e-7, "5e-7"},
      // Rounding that carries into a new magnitude: shortest round-trip is 1e+23.
      {9.999999999999999e22, "1e+23"},
      // Decimal-form boundaries around n in (-6, 0].
      {0.0001, "0.0001"},
      {0.00001, "0.00001"},
      {0.5, "0.5"},
  };
  for (const auto& [value, expected] : corpus) {
    EXPECT_EQ(JsNumberToString(value), expected) << "value: " << value;
  }
}

// Shared accept/reject corpus, identical to the TypeScript string_util test.
// Both surfaces must agree token-for-token.
TEST(StringUtilTest, IsNumericSharedCorpus) {
  const std::vector<std::pair<std::string, bool>> corpus = {
      {"123", true},     {"-12.5", true}, {"+.5", true},       {"12.", true},    {"1e9", true},
      {"-3.0E-2", true}, {"inf", false},  {"Infinity", false}, {"nan", false},   {"0x1f", false},
      {" 5 ", false},    {"", false},     {"1.2.3", false},    {"1,000", false},
  };
  for (const auto& [input, expected] : corpus) {
    EXPECT_EQ(IsNumeric(input), expected) << "input: '" << input << "'";
  }
}

// Bit-exact parsing is what makes a model reproducible: the standard library
// backends this file can compile against disagree about which decimals are
// "out of range", so a subnormal must not be allowed to depend on the platform
// or on which backend was available at build time. Comparing bit patterns
// rather than values also keeps the two signed zeros apart.
TEST(StringUtilTest, ToDoubleMatchesJavaScriptOnEveryCorpusDecimal) {
  for (const auto& numeric_case : kNumericParseCases) {
    ASSERT_TRUE(IsNumeric(numeric_case.text)) << "input: " << numeric_case.text;
    EXPECT_EQ(DoubleToBits(ToDouble(numeric_case.text)), numeric_case.bits)
        << "input: " << numeric_case.text;
  }
}

// The same corpus drives the constraint-literal entry point, which the parser
// uses for every relational literal: a representable decimal (subnormals
// included) is accepted with its exact value, and only overflow or underflow
// to zero is rejected.
TEST(StringUtilTest, TryParseFiniteDoubleClassifiesEveryCorpusDecimal) {
  for (const auto& numeric_case : kNumericParseCases) {
    constexpr double kUntouched = 12345.0;
    double parsed = kUntouched;
    EXPECT_EQ(TryParseFiniteDouble(numeric_case.text, &parsed), numeric_case.accepts_literal)
        << "input: " << numeric_case.text;
    if (numeric_case.accepts_literal) {
      EXPECT_EQ(DoubleToBits(parsed), numeric_case.bits) << "input: " << numeric_case.text;
    } else {
      EXPECT_EQ(parsed, kUntouched) << "input: " << numeric_case.text;
    }
  }
}

TEST(StringUtilTest, IsNumericAcceptsDecimalForms) {
  EXPECT_TRUE(IsNumeric("0"));
  EXPECT_TRUE(IsNumeric("3.14"));
  EXPECT_TRUE(IsNumeric(".5"));
  EXPECT_TRUE(IsNumeric("12."));
  EXPECT_TRUE(IsNumeric("-0"));
  EXPECT_TRUE(IsNumeric("+5"));
  EXPECT_TRUE(IsNumeric("1e10"));
  EXPECT_TRUE(IsNumeric("2.5e-3"));
}

TEST(StringUtilTest, IsNumericRejectsMalformed) {
  EXPECT_FALSE(IsNumeric("abc"));
  EXPECT_FALSE(IsNumeric("12abc"));
  EXPECT_FALSE(IsNumeric(" 5"));
  EXPECT_FALSE(IsNumeric("5 "));
  EXPECT_FALSE(IsNumeric("\t"));
  EXPECT_FALSE(IsNumeric("INF"));
  EXPECT_FALSE(IsNumeric("NaN"));
  EXPECT_FALSE(IsNumeric("1e"));
  EXPECT_FALSE(IsNumeric("."));
  EXPECT_FALSE(IsNumeric("+"));
  EXPECT_FALSE(IsNumeric("e5"));
}

TEST(StringUtilTest, CaseInsensitiveEqualAsciiOnly) {
  // ASCII case differences are equal.
  EXPECT_TRUE(CaseInsensitiveEqual("Os", "os"));
  EXPECT_TRUE(CaseInsensitiveEqual("CHROME", "chrome"));

  // Non-ASCII case differences are NOT folded (bytes >= 0x80 compared exactly).
  EXPECT_FALSE(CaseInsensitiveEqual("\xC3\xA9", "\xC3\x89"));  // é vs É (UTF-8)

  // ASCII portion of a mixed string still folds.
  EXPECT_TRUE(CaseInsensitiveEqual("caF\xC3\xA9", "CAF\xC3\xA9"));

  EXPECT_FALSE(CaseInsensitiveEqual("ab", "abc"));
}

TEST(StringUtilTest, NumericParsingAndAsciiFoldingIgnoreProcessLocale) {
  const char* previous_numeric = std::setlocale(LC_NUMERIC, nullptr);
  ASSERT_NE(previous_numeric, nullptr);
  const std::string saved_numeric(previous_numeric);
  const char* previous_ctype = std::setlocale(LC_CTYPE, nullptr);
  ASSERT_NE(previous_ctype, nullptr);
  const std::string saved_ctype(previous_ctype);

  if (std::setlocale(LC_NUMERIC, "de_DE.UTF-8") == nullptr ||
      std::setlocale(LC_CTYPE, "tr_TR.UTF-8") == nullptr) {
    std::setlocale(LC_NUMERIC, saved_numeric.c_str());
    std::setlocale(LC_CTYPE, saved_ctype.c_str());
    GTEST_SKIP() << "Required locale is unavailable";
  }

  EXPECT_DOUBLE_EQ(ToDouble("1.5"), 1.5);
  EXPECT_DOUBLE_EQ(ToDouble("+.5"), 0.5);
  EXPECT_DOUBLE_EQ(ToDouble("12."), 12.0);
  EXPECT_DOUBLE_EQ(ToDouble("-3.0E-2"), -0.03);
  EXPECT_EQ(ToDouble("1e-9999"), 0.0);
  EXPECT_TRUE(std::isinf(ToDouble("1e9999")));
  EXPECT_TRUE(CaseInsensitiveEqual("I", "i"));

  EXPECT_NE(std::setlocale(LC_NUMERIC, saved_numeric.c_str()), nullptr);
  EXPECT_NE(std::setlocale(LC_CTYPE, saved_ctype.c_str()), nullptr);
}

}  // namespace
