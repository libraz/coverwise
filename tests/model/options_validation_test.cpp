/// @file options_validation_test.cpp
/// @brief The acceptance gate: what it accepts, and what cannot bypass it.

#include "model/options_validation.h"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

#include "model/boundary.h"
#include "model/limits.h"
#include "model/parameter.h"

namespace {

using coverwise::model::AcceptedOptions;
using coverwise::model::AcceptOptions;
using coverwise::model::BoundaryConfig;
using coverwise::model::Error;
using coverwise::model::GenerateOptions;
using coverwise::model::Parameter;
using coverwise::model::ValidatedOptions;

// The point of the gate is not that validation happens to be called, but that
// there is no way to end up with a ValidatedOptions without calling it. A
// GenerateOptions can be built by anyone — surfaces have to build one — so the
// guarantee has to sit on the type the engine call sites take. If a later edit
// drops the AcceptOptions call, these are the assertions that stop compiling.
static_assert(!std::is_default_constructible_v<ValidatedOptions>,
              "ValidatedOptions must not be constructible without the gate");
static_assert(!std::is_constructible_v<ValidatedOptions, GenerateOptions>,
              "ValidatedOptions must not be constructible from raw options");
static_assert(!std::is_constructible_v<ValidatedOptions, const GenerateOptions&>,
              "ValidatedOptions must not be constructible from raw options");
static_assert(!std::is_constructible_v<AcceptedOptions, Error>,
              "An acceptance verdict must not be fabricated from an error");
static_assert(!std::is_default_constructible_v<AcceptedOptions>,
              "An acceptance verdict must not be fabricated");
static_assert(
    std::is_same_v<decltype(std::declval<const ValidatedOptions&>().get()), const GenerateOptions&>,
    "Validated options must be readable, and only readable");

GenerateOptions TwoBinaryParameters() {
  GenerateOptions options;
  options.parameters.emplace_back("a", std::vector<std::string>{"0", "1"});
  options.parameters.emplace_back("b", std::vector<std::string>{"0", "1"});
  return options;
}

TEST(OptionsGateTest, AcceptsAWellFormedModel) {
  auto accepted = AcceptOptions(TwoBinaryParameters());
  ASSERT_TRUE(accepted.ok()) << accepted.error().message;
  EXPECT_EQ(accepted->get().parameters.size(), 2u);
}

TEST(OptionsGateTest, RejectionCarriesTheReasonAndNoOptions) {
  GenerateOptions options = TwoBinaryParameters();
  options.strength = 5;
  auto accepted = AcceptOptions(std::move(options));
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().code, Error::Code::kInvalidInput);
  EXPECT_EQ(accepted.error().message, "Strength must be between 1 and parameter count");
}

// A boundary parameter may spell out only the values it wants marked invalid and
// leave the valid ones to the range. Judging the declared list would call that
// model valueless; judging the expanded list is what makes it well-formed.
TEST(OptionsGateTest, AcceptsABoundaryParameterWhoseOnlyDeclaredValueIsInvalid) {
  GenerateOptions options;
  options.parameters.emplace_back("age", std::vector<std::string>{"999"}, std::vector<bool>{true});
  options.parameters.emplace_back("mode", std::vector<std::string>{"a", "b"});
  options.boundary_configs["age"] = {BoundaryConfig::Type::kInteger, 0, 10, 1.0};

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_TRUE(accepted.ok()) << accepted.error().message;
  const auto& age = accepted->get().parameters[0];
  EXPECT_EQ(age.values, (std::vector<std::string>{"-1", "0", "1", "9", "10", "11", "999"}));
  EXPECT_EQ(age.valid_count(), 6u);
  EXPECT_TRUE(age.is_invalid(age.find_value_index("999")));
  EXPECT_TRUE(accepted->get().boundary_configs.empty());
}

// Integer expansion steps by one, so accepting any other step would generate a
// value set the caller did not ask for.
TEST(OptionsGateTest, RejectsAnIntegerBoundaryStepOtherThanOne) {
  GenerateOptions options;
  options.parameters.emplace_back("n", std::vector<std::string>{});
  options.parameters.emplace_back("m", std::vector<std::string>{"a", "b"});
  options.boundary_configs["n"] = {BoundaryConfig::Type::kInteger, 0, 10, 5.0};

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, "Integer boundary step must be 1 for parameter n");
}

TEST(OptionsGateTest, AcceptsAnIntegerBoundaryStepOfOne) {
  GenerateOptions options;
  options.parameters.emplace_back("n", std::vector<std::string>{});
  options.parameters.emplace_back("m", std::vector<std::string>{"a", "b"});
  options.boundary_configs["n"] = {BoundaryConfig::Type::kInteger, 0, 10, 1.0};

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_TRUE(accepted.ok()) << accepted.error().message;
  EXPECT_EQ(accepted->get().parameters[0].values,
            (std::vector<std::string>{"-1", "0", "1", "9", "10", "11"}));
}

TEST(OptionsGateTest, RejectsMoreValuesThanOneParameterMayDeclare) {
  GenerateOptions options = TwoBinaryParameters();
  std::vector<std::string> values;
  values.reserve(coverwise::model::kMaxValuesPerParameter + 1);
  for (size_t i = 0; i <= coverwise::model::kMaxValuesPerParameter; ++i) {
    values.push_back(std::to_string(i));
  }
  options.parameters.emplace_back("wide", std::move(values));

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_FALSE(accepted.ok());
  EXPECT_NE(accepted.error().message.find("has too many values"), std::string::npos)
      << accepted.error().message;
}

TEST(OptionsGateTest, RejectsMoreConstraintsThanOneModelMayCarry) {
  GenerateOptions options = TwoBinaryParameters();
  for (size_t i = 0; i <= coverwise::model::kMaxConstraints; ++i) {
    options.constraint_expressions.push_back("a = 0");
  }

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_FALSE(accepted.ok());
  EXPECT_NE(accepted.error().message.find("exceeds maximum"), std::string::npos)
      << accepted.error().message;
}

// The documented bound on a whole input is the aggregate string budget, and it
// is charged over the model rather than over any surface's byte stream.
TEST(OptionsGateTest, RejectsStringDataBeyondTheAggregateBudget) {
  GenerateOptions options = TwoBinaryParameters();
  std::vector<std::string> values;
  const size_t value_bytes = coverwise::model::kMaxStringBytes;
  const size_t value_count = coverwise::model::kMaxAggregateStringBytes / value_bytes + 2;
  for (size_t i = 0; i < value_count; ++i) {
    values.push_back(std::string(value_bytes - 8, 'x') + std::to_string(1000000 + i));
  }
  options.parameters.emplace_back("wide", std::move(values));

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message,
            "Input strings exceed " + std::to_string(coverwise::model::kMaxAggregateStringBytes) +
                " UTF-8 bytes");
}

TEST(OptionsGateTest, RejectsASingleStringBeyondThePerStringBudget) {
  GenerateOptions options = TwoBinaryParameters();
  options.parameters.emplace_back(
      "wide", std::vector<std::string>{std::string(coverwise::model::kMaxStringBytes + 1, 'x')});

  auto accepted = AcceptOptions(std::move(options));
  ASSERT_FALSE(accepted.ok());
  EXPECT_NE(accepted.error().message.find("UTF-8 bytes"), std::string::npos)
      << accepted.error().message;
}

// ExpandBoundaries is exposed so a surface can resolve rows against the final
// value list. Running it twice must land in the same place as running it once,
// which is what makes the split a convenience rather than a way past the gate.
TEST(OptionsGateTest, ExpandingBeforeTheGateChangesNothing) {
  GenerateOptions pre_expanded;
  pre_expanded.parameters.emplace_back("n", std::vector<std::string>{});
  pre_expanded.parameters.emplace_back("m", std::vector<std::string>{"a", "b"});
  pre_expanded.boundary_configs["n"] = {BoundaryConfig::Type::kInteger, 0, 10, 1.0};
  GenerateOptions direct = pre_expanded;

  ASSERT_TRUE(coverwise::model::ExpandBoundaries(pre_expanded).ok());
  auto from_expanded = AcceptOptions(std::move(pre_expanded));
  auto from_direct = AcceptOptions(std::move(direct));

  ASSERT_TRUE(from_expanded.ok()) << from_expanded.error().message;
  ASSERT_TRUE(from_direct.ok()) << from_direct.error().message;
  EXPECT_EQ(from_expanded->get().parameters[0].values, from_direct->get().parameters[0].values);
}

}  // namespace
