#include "core/constraint_solver.h"

#include <gtest/gtest.h>

#include <algorithm>
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
using coverwise::core::SolveStack;
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

namespace {

/// @brief One search outcome, recorded in full so two runs can be compared.
struct SearchOutcome {
  bool satisfiable = false;
  std::vector<uint32_t> witness;
  uint64_t remaining = 0;
  bool exceeded = false;

  bool operator==(const SearchOutcome& other) const {
    return satisfiable == other.satisfiable && witness == other.witness &&
           remaining == other.remaining && exceeded == other.exceeded;
  }
};

/// @brief Solve every pair of pinned values in the model, one search per pair.
///
/// This is the shape the coverage sweep drives: a long run of searches over one
/// model, each starting from a different two-parameter partial assignment.
/// @param stack Frame buffer shared across the run, or nullptr for a private
///        one per search.
std::vector<SearchOutcome> SolveEveryPair(const std::vector<Parameter>& params,
                                          const std::vector<Constraint>& constraints,
                                          SolveStack* stack) {
  std::vector<SearchOutcome> outcomes;
  for (uint32_t left = 0; left < params.size(); ++left) {
    for (uint32_t right = left + 1; right < params.size(); ++right) {
      for (uint32_t lv = 0; lv < params[left].size(); ++lv) {
        for (uint32_t rv = 0; rv < params[right].size(); ++rv) {
          TestCase witness;
          witness.values.assign(params.size(), kUnassigned);
          witness.values[left] = lv;
          witness.values[right] = rv;
          SolveBudget budget;
          SearchOutcome outcome;
          outcome.satisfiable =
              CompleteValidAssignment(params, constraints, witness, &budget, nullptr, stack);
          outcome.witness = witness.values;
          outcome.remaining = budget.remaining;
          outcome.exceeded = budget.exceeded;
          outcomes.push_back(std::move(outcome));
        }
      }
    }
  }
  return outcomes;
}

std::vector<Constraint> ParseConstraints(const std::vector<std::string>& expressions,
                                         const std::vector<Parameter>& params) {
  std::vector<Constraint> constraints;
  for (const auto& expression : expressions) {
    auto parse = coverwise::model::ParseConstraint(expression, params);
    EXPECT_TRUE(parse.error.ok()) << parse.error.message << ": " << parse.error.detail;
    constraints.push_back(std::move(parse.constraint));
  }
  return constraints;
}

}  // namespace

// A frame buffer handed in by the caller is scratch and nothing else: every
// search reports the same verdict, the same witness and the same budget
// arithmetic whether it got a buffer of its own or one a previous search left
// behind. Interacting implications make some of these pairs infeasible and
// others reachable only after backtracking, so the run exercises both exits.
TEST(ConstraintSolverTest, AReusedFrameBufferDoesNotChangeAnySearchOutcome) {
  auto params = MakeBinaryParams(9);
  auto expressions = std::vector<std::string>{
      "IF P0=\"a\" THEN P1=\"b\"", "IF P1=\"b\" THEN P2=\"a\"",   "IF P2=\"a\" THEN P3=\"b\"",
      "P3=\"a\" OR P4=\"a\"",      "NOT (P5=\"a\" AND P6=\"a\")", "IF P7=\"b\" THEN P8=\"b\"",
  };
  auto fresh_constraints = ParseConstraints(expressions, params);
  auto reused_constraints = ParseConstraints(expressions, params);

  const auto fresh = SolveEveryPair(params, fresh_constraints, nullptr);
  SolveStack shared;
  const auto reused = SolveEveryPair(params, reused_constraints, &shared);

  ASSERT_EQ(fresh.size(), reused.size());
  EXPECT_EQ(fresh, reused);

  // The run has to contain both verdicts, or it fixes nothing.
  EXPECT_TRUE(std::any_of(fresh.begin(), fresh.end(),
                          [](const SearchOutcome& o) { return o.satisfiable; }));
  EXPECT_TRUE(std::any_of(fresh.begin(), fresh.end(),
                          [](const SearchOutcome& o) { return !o.satisfiable; }));
}

// The budget-exhausted exit leaves frames on the stack that no backtrack pops.
// Reusing that buffer for the next search must still report an exhausted budget
// and an untouched assignment rather than resuming from the leftovers.
TEST(ConstraintSolverTest, AStackLeftBehindByAnExhaustedSearchIsNotResumed) {
  constexpr uint32_t kParameters = 24;
  auto params = MakeBinaryParams(kParameters);
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<CompleteAssignmentOnlyConstraint>());

  SolveStack shared;
  for (int attempt = 0; attempt < 2; ++attempt) {
    TestCase witness;
    witness.values.assign(kParameters, kUnassigned);
    // A budget far below the default still runs out mid-descent, which is the
    // exit under test, and keeps the run short enough to repeat.
    SolveBudget budget{10'000, false};
    ASSERT_FALSE(CompleteValidAssignment(params, constraints, witness, &budget, nullptr, &shared))
        << "attempt " << attempt;
    EXPECT_TRUE(budget.exceeded);
    EXPECT_EQ(budget.remaining, 0u);
    for (uint32_t value : witness.values) {
      EXPECT_EQ(value, kUnassigned);
    }
  }
}

// The same holds for the masked entry point, which reaches the search through a
// different argument path.
TEST(ConstraintSolverTest, AReusedFrameBufferDoesNotChangeAMaskedSearch) {
  auto params = MakeBinaryParams(6);
  auto expressions = std::vector<std::string>{
      "IF P0=\"a\" THEN P1=\"b\"",
      "NOT (P2=\"a\" AND P3=\"a\")",
  };
  auto fresh_constraints = ParseConstraints(expressions, params);
  auto reused_constraints = ParseConstraints(expressions, params);
  auto mask = MakeAllowAllMask(params);

  SolveStack shared;
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      TestCase fresh_witness;
      fresh_witness.values.assign(params.size(), kUnassigned);
      fresh_witness.values[pi] = vi;
      TestCase reused_witness = fresh_witness;

      SolveBudget fresh_budget;
      SolveBudget reused_budget;
      const bool fresh_ok = CompleteAssignment(params, fresh_constraints, mask, fresh_witness,
                                               &fresh_budget, nullptr, nullptr);
      const bool reused_ok = CompleteAssignment(params, reused_constraints, mask, reused_witness,
                                                &reused_budget, nullptr, &shared);

      EXPECT_EQ(fresh_ok, reused_ok);
      EXPECT_EQ(fresh_witness.values, reused_witness.values);
      EXPECT_EQ(fresh_budget.remaining, reused_budget.remaining);
      EXPECT_EQ(fresh_budget.exceeded, reused_budget.exceeded);
    }
  }
}
