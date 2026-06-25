#include "util/string_util.h"

#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

using coverwise::util::CaseInsensitiveEqual;
using coverwise::util::IsNumeric;

namespace {

// Shared accept/reject corpus, identical to the TypeScript string_util test.
// Both surfaces must agree token-for-token.
TEST(StringUtilTest, IsNumericSharedCorpus) {
  const std::vector<std::pair<std::string, bool>> corpus = {
      {"123", true},      {"-12.5", true}, {"+.5", true},  {"12.", true},
      {"1e9", true},      {"-3.0E-2", true}, {"inf", false}, {"Infinity", false},
      {"nan", false},     {"0x1f", false}, {" 5 ", false}, {"", false},
      {"1.2.3", false},   {"1,000", false},
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
