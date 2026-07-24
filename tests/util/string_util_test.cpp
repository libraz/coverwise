#include "util/string_util.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using coverwise::util::CaseInsensitiveEqual;
using coverwise::util::IsNumeric;
using coverwise::util::JsNumberToString;

namespace {

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

}  // namespace
