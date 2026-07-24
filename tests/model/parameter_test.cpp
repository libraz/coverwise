#include "model/parameter.h"

#include <gtest/gtest.h>

#include <vector>

#include "model/error.h"

using coverwise::model::Error;
using coverwise::model::Parameter;
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
