#include "model/parameter.h"

#include <gtest/gtest.h>

#include <vector>

#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/error.h"

using coverwise::model::EqualsNode;
using coverwise::model::Error;
using coverwise::model::Parameter;
using coverwise::model::ParseConstraint;
using coverwise::model::ResolveValueName;
using coverwise::model::ValidateParameters;

TEST(ParameterTest, SizeReturnsValueCount) {
  Parameter p{"color", {"red", "green", "blue"}, {}};
  EXPECT_EQ(p.size(), 3u);
}

TEST(ParameterTest, EmptyParameter) {
  Parameter p{"empty", {}, {}};
  EXPECT_EQ(p.size(), 0u);
}

TEST(ParameterTest, SingleValue) {
  Parameter p{"flag", {"on"}, {}};
  EXPECT_EQ(p.size(), 1u);
}

TEST(ValidateParametersTest, AcceptsWellFormedCollection) {
  std::vector<Parameter> params{
      Parameter{"os", {"win", "mac"}},
      Parameter{"browser", {"chrome", "safari"}},
  };
  EXPECT_TRUE(ValidateParameters(params).ok());
}

TEST(ValidateParametersTest, RejectsEmptyName) {
  std::vector<Parameter> params{Parameter{"", {"a"}}};
  Error err = ValidateParameters(params);
  EXPECT_EQ(err.code, Error::Code::kInvalidInput);
  EXPECT_EQ(err.message, "Parameter name must be a non-empty string");
}

TEST(ValidateParametersTest, RejectsEmptyValues) {
  std::vector<Parameter> params{Parameter{"os", {}}};
  Error err = ValidateParameters(params);
  EXPECT_EQ(err.code, Error::Code::kInvalidInput);
  EXPECT_EQ(err.message, "Parameter 'os' must have at least one value");
}

TEST(ValidateParametersTest, RejectsParameterWithNoValidValues) {
  std::vector<Parameter> params{Parameter{"os", {"win", "mac"}, {true, true}}};
  Error err = ValidateParameters(params);
  EXPECT_EQ(err.code, Error::Code::kInvalidInput);
  EXPECT_EQ(err.message, "Parameter 'os' must have at least one valid value");
}

TEST(ValidateParametersTest, RejectsDuplicateValue) {
  std::vector<Parameter> params{Parameter{"os", {"win", "win"}}};
  Error err = ValidateParameters(params);
  EXPECT_EQ(err.code, Error::Code::kInvalidInput);
  EXPECT_EQ(err.message, "Duplicate value 'win' in parameter 'os'");
}

TEST(ValidateParametersTest, RejectsDuplicateParameterName) {
  std::vector<Parameter> params{
      Parameter{"os", {"win"}},
      Parameter{"os", {"mac"}},
  };
  Error err = ValidateParameters(params);
  EXPECT_EQ(err.code, Error::Code::kInvalidInput);
  EXPECT_EQ(err.message, "Duplicate parameter name 'os'");
}

TEST(ValidateParametersTest, RejectsParameterNamesDifferingOnlyByAsciiCase) {
  std::vector<Parameter> params{
      Parameter{"OS", {"win"}},
      Parameter{"os", {"linux"}},
  };
  Error err = ValidateParameters(params);
  EXPECT_EQ(err.code, Error::Code::kInvalidInput);
  EXPECT_EQ(err.message, "Parameter names must not differ only by ASCII case: 'os'");
}

TEST(ValidateParametersTest, RejectsAliasPrimaryAndCaseOnlyCollisions) {
  Parameter alias_collision{"browser", {"chrome", "safari"}};
  alias_collision.set_aliases({{"safari"}, {}});
  EXPECT_EQ(coverwise::model::ValidateParameters({alias_collision}).code,
            coverwise::model::Error::Code::kInvalidInput);

  Parameter case_collision{"os", {"OS", "other"}};
  case_collision.set_aliases({{}, {"os"}});
  EXPECT_EQ(coverwise::model::ValidateParameters({case_collision}).code,
            coverwise::model::Error::Code::kInvalidInput);
}

// The fold exists to decide equality, and a decision is all it is allowed to
// produce: the case a caller wrote is the case that comes back, so which way
// the fold happens to map letters stays invisible from outside.
TEST(ValidateParametersTest, ARejectionQuotesTheTextTheCallerWroteNotTheFoldedForm) {
  const Error duplicate_name = ValidateParameters({
      Parameter{"OsName", {"a"}},
      Parameter{"osNAME", {"b"}},
  });
  ASSERT_EQ(duplicate_name.code, Error::Code::kInvalidInput);
  EXPECT_EQ(duplicate_name.message, "Parameter names must not differ only by ASCII case: 'osNAME'");

  const Error ambiguous_value = ValidateParameters({Parameter{"Browser", {"Chrome", "CHROME"}}});
  ASSERT_EQ(ambiguous_value.code, Error::Code::kInvalidInput);
  EXPECT_EQ(ambiguous_value.message, "Ambiguous value or alias 'CHROME' in parameter 'Browser'");

  Parameter aliased{"Browser", {"Chrome", "Edge"}};
  aliased.set_aliases({{}, {"chROME"}});
  const Error ambiguous_alias = ValidateParameters({aliased});
  ASSERT_EQ(ambiguous_alias.code, Error::Code::kInvalidInput);
  EXPECT_EQ(ambiguous_alias.message, "Ambiguous value or alias 'chROME' in parameter 'Browser'");
}

TEST(ParameterTest, ResolvingAValueByEitherCaseLeavesTheStoredSpellingAlone) {
  const Parameter p{"Browser", {"Chrome", "FireFox"}};

  for (const char* spelling : {"Chrome", "chrome", "CHROME", "cHrOmE"}) {
    EXPECT_EQ(p.find_value_index(spelling, /*case_sensitive=*/false), 0u) << spelling;
  }
  EXPECT_EQ(p.find_value_index("chrome", /*case_sensitive=*/true), UINT32_MAX);

  // Whatever spelling resolved it, the value itself is untouched.
  EXPECT_EQ(p.values[0], "Chrome");
  EXPECT_EQ(p.values[1], "FireFox");
  EXPECT_EQ(p.display_name(1, 0), "FireFox");
}

// The entry point every surface resolves caller text through carries the policy
// itself: no argument decides it, so no call site can pick a different one.
TEST(ResolveValueNameTest, AnyAsciiCaseOfAValueOrAliasNamesTheDeclaredIndex) {
  Parameter p{"os", {"Windows", "Linux"}};
  p.set_aliases({{"Win32"}, {}});

  for (const char* spelling : {"Windows", "windows", "WINDOWS", "wInDoWs"}) {
    EXPECT_EQ(ResolveValueName(p, spelling), 0u) << spelling;
  }
  for (const char* spelling : {"Win32", "win32", "WIN32"}) {
    EXPECT_EQ(ResolveValueName(p, spelling), 0u) << spelling;
  }
  EXPECT_EQ(ResolveValueName(p, "LINUX"), 1u);
  EXPECT_EQ(ResolveValueName(p, "Windows"), ResolveValueName(p, "windows"));
}

// Widening the fold past ASCII would be its own defect: it would make two names
// the model is entitled to treat as distinct resolve to one value.
TEST(ResolveValueNameTest, TheFoldReachesAsciiLettersOnly) {
  const Parameter p{"city", {"MÜNCHEN", "OSAKA"}};

  EXPECT_EQ(ResolveValueName(p, "MÜNCHEN"), 0u);
  // Every ASCII letter matches; only the U-umlaut differs in case, and that
  // difference is preserved, so the name does not resolve.
  EXPECT_EQ(ResolveValueName(p, "MüNCHEN"), UINT32_MAX);
  // The ASCII half of the same value still folds.
  EXPECT_EQ(ResolveValueName(p, "osaka"), 1u);
}

TEST(ResolveValueNameTest, ANameNoValueOrAliasCarriesIsStillUnknown) {
  Parameter p{"os", {"Windows"}};
  p.set_aliases({{"Win32"}});

  EXPECT_EQ(ResolveValueName(p, "Linux"), UINT32_MAX);
  EXPECT_EQ(ResolveValueName(p, "win"), UINT32_MAX);
  EXPECT_EQ(ResolveValueName(p, ""), UINT32_MAX);
}

// A row and a constraint can name the same value, and the model is only
// coherent if they name the same one. This is what settles the policy: the
// constraint path has always folded, so a row that did not was reading the same
// text a second way.
TEST(ResolveValueNameTest, AConstraintOperandResolvesToTheSameIndex) {
  const std::vector<Parameter> params{Parameter{"os", {"Windows", "Linux"}},
                                      Parameter{"browser", {"Chrome", "Firefox"}}};

  const auto parsed = ParseConstraint("os = wInDoWs", params, {});
  ASSERT_TRUE(parsed.error.ok()) << parsed.error.message;
  const auto* equals = dynamic_cast<const EqualsNode*>(parsed.constraint.get());
  ASSERT_NE(equals, nullptr);

  EXPECT_EQ(equals->value_index(), ResolveValueName(params[0], "wInDoWs"));
  EXPECT_EQ(equals->value_index(), ResolveValueName(params[0], "Windows"));
}
