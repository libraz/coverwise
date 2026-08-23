#include "core/constraint_solver.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/generator.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/generate_options.h"
#include "model/parameter.h"
#include "model/test_case.h"

using coverwise::core::CompleteAssignment;
using coverwise::core::CompleteValidAssignment;
using coverwise::core::SolveBudget;
using coverwise::model::Constraint;
using coverwise::model::ConstraintResult;
using coverwise::model::kUnassigned;
using coverwise::model::Parameter;
using coverwise::model::TestCase;

namespace {

/// @brief Constraint that stays undecided until every parameter is assigned.
///
/// Nothing can be pruned before a complete assignment, and the only satisfying
/// one is "all values at index 1", which the search reaches last. Proving it
/// therefore costs more nodes than the budget allows.
class CompleteAssignmentOnlyConstraint final : public coverwise::model::ConstraintNode {
 public:
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override {
    for (uint32_t value : assignment) {
      if (value == kUnassigned) return ConstraintResult::kUnknown;
    }
    for (uint32_t value : assignment) {
      if (value != 1) return ConstraintResult::kFalse;
    }
    return ConstraintResult::kTrue;
  }
};

std::vector<Parameter> MakeBinaryParams(uint32_t count) {
  std::vector<Parameter> params;
  params.reserve(count);
  for (uint32_t index = 0; index < count; ++index) {
    params.push_back(Parameter{"P" + std::to_string(index), {"a", "b"}});
  }
  return params;
}

std::vector<Constraint> ParseSingleConstraint(const std::string& expression,
                                              const std::vector<Parameter>& params) {
  auto parse = coverwise::model::ParseConstraint(expression, params);
  EXPECT_TRUE(parse.error.ok()) << parse.error.message << ": " << parse.error.detail;
  std::vector<Constraint> constraints;
  constraints.push_back(std::move(parse.constraint));
  return constraints;
}

std::vector<std::vector<bool>> MakeAllowAllMask(const std::vector<Parameter>& params) {
  std::vector<std::vector<bool>> mask;
  mask.reserve(params.size());
  for (const auto& parameter : params) {
    mask.emplace_back(parameter.size(), true);
  }
  return mask;
}

}  // namespace

TEST(ConstraintSolverTest, CompletesAModelDeeperThanTheCallStackAllows) {
  // A satisfiable chain spends one node of the budget per parameter, so a
  // recursive descent would run out of stack long before the node budget bites.
  constexpr uint32_t kParameters = 200'000;
  auto params = MakeBinaryParams(kParameters);
  auto constraints = ParseSingleConstraint("P0=\"a\" OR P1=\"b\"", params);

  TestCase witness;
  witness.values.assign(kParameters, kUnassigned);
  SolveBudget budget;
  ASSERT_TRUE(CompleteValidAssignment(params, constraints, witness, &budget));

  EXPECT_FALSE(budget.exceeded);
  ASSERT_EQ(witness.values.size(), kParameters);
  for (uint32_t value : witness.values) {
    EXPECT_NE(value, kUnassigned);
  }
}

TEST(ConstraintSolverTest, CompletesADeepModelThroughTheAllowedValueMask) {
  constexpr uint32_t kParameters = 200'000;
  auto params = MakeBinaryParams(kParameters);
  auto constraints = ParseSingleConstraint("P0=\"a\" OR P1=\"b\"", params);
  auto mask = MakeAllowAllMask(params);

  TestCase witness;
  witness.values.assign(kParameters, kUnassigned);
  SolveBudget budget;
  ASSERT_TRUE(CompleteAssignment(params, constraints, mask, witness, &budget));

  EXPECT_FALSE(budget.exceeded);
  for (uint32_t value : witness.values) {
    EXPECT_NE(value, kUnassigned);
  }
}

TEST(ConstraintSolverTest, FindsAWitnessThatRequiresBacktracking) {
  auto params = MakeBinaryParams(3);
  auto constraints = ParseSingleConstraint("P0=\"b\" AND P1=\"b\" AND P2=\"b\"", params);

  TestCase witness;
  witness.values.assign(3, kUnassigned);
  ASSERT_TRUE(CompleteValidAssignment(params, constraints, witness));

  EXPECT_EQ(witness.values, (std::vector<uint32_t>{1, 1, 1}));
}

TEST(ConstraintSolverTest, KeepsValuesTheCallerPinnedInTheWitness) {
  auto params = MakeBinaryParams(3);
  auto constraints = ParseSingleConstraint("P1=\"b\"", params);

  TestCase witness;
  witness.values = {1, kUnassigned, kUnassigned};
  ASSERT_TRUE(CompleteValidAssignment(params, constraints, witness));

  EXPECT_EQ(witness.values[0], 1u);
  EXPECT_EQ(witness.values[1], 1u);
  EXPECT_NE(witness.values[2], kUnassigned);
}

TEST(ConstraintSolverTest, RestoresThePartialAssignmentWhenNoWitnessExists) {
  auto params = MakeBinaryParams(3);
  auto parse_first = coverwise::model::ParseConstraint("P0=\"a\"", params);
  auto parse_second = coverwise::model::ParseConstraint("P0=\"b\"", params);
  ASSERT_TRUE(parse_first.error.ok());
  ASSERT_TRUE(parse_second.error.ok());
  std::vector<Constraint> constraints;
  constraints.push_back(std::move(parse_first.constraint));
  constraints.push_back(std::move(parse_second.constraint));

  TestCase witness;
  witness.values.assign(3, kUnassigned);
  SolveBudget budget;
  ASSERT_FALSE(CompleteValidAssignment(params, constraints, witness, &budget));

  EXPECT_FALSE(budget.exceeded);
  EXPECT_EQ(witness.values, (std::vector<uint32_t>{kUnassigned, kUnassigned, kUnassigned}));
}

TEST(ParameterLimitTest, GenerateRejectsAParameterCountBeyondTheDocumentedLimit) {
  // Generation completes an assignment per test case, one parameter per search
  // level, so an oversized model has to be turned away as invalid input rather
  // than handed to a search that cannot finish.
  constexpr uint32_t kParameters = 200'000;
  coverwise::model::GenerateOptions options;
  options.parameters = MakeBinaryParams(kParameters);
  options.strength = 1;
  options.constraint_expressions.emplace_back("P0=\"a\" OR P1=\"b\"");

  auto result = coverwise::core::Generate(options);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_TRUE(result.tests.empty());
}

TEST(ParameterLimitTest, GenerateAcceptsTheLargestDocumentedParameterCount) {
  constexpr uint32_t kParameters = coverwise::model::kMaxParameters;
  coverwise::model::GenerateOptions options;
  options.parameters = MakeBinaryParams(kParameters);
  options.strength = 1;
  options.constraint_expressions.emplace_back("P0=\"a\" OR P1=\"b\"");

  auto result = coverwise::core::Generate(options);

  EXPECT_TRUE(result.error.ok()) << result.error.message << ": " << result.error.detail;
  EXPECT_FALSE(result.tests.empty());
}

TEST(ParameterLimitTest, GenerateRejectsOneParameterPastTheLimit) {
  coverwise::model::GenerateOptions options;
  options.parameters = MakeBinaryParams(coverwise::model::kMaxParameters + 1);
  options.strength = 1;

  auto result = coverwise::core::Generate(options);

  EXPECT_EQ(result.error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_EQ(result.error.message, "Parameter count 1025 exceeds maximum of 1024");
}

TEST(ConstraintSolverTest, ReportsAnExhaustedBudgetAndUnwindsTheAssignment) {
  constexpr uint32_t kParameters = 24;
  auto params = MakeBinaryParams(kParameters);
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<CompleteAssignmentOnlyConstraint>());

  TestCase witness;
  witness.values.assign(kParameters, kUnassigned);
  SolveBudget budget;
  ASSERT_FALSE(CompleteValidAssignment(params, constraints, witness, &budget));

  EXPECT_TRUE(budget.exceeded);
  EXPECT_EQ(budget.remaining, 0u);
  for (uint32_t value : witness.values) {
    EXPECT_EQ(value, kUnassigned);
  }
}
