/// @file constraint_parser_test.cpp
/// @brief Tests for constraint expression parsing and evaluation.

#include "model/constraint_parser.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"

using coverwise::model::ConstraintResult;
using coverwise::model::Parameter;
using coverwise::model::ParseConstraint;
using coverwise::model::ParseResult;

namespace {

constexpr uint32_t kUnassigned = UINT32_MAX;

std::vector<Parameter> MakeParams() {
  return {
      {"os", {"win", "mac", "linux"}, {}},
      {"browser", {"chrome", "firefox", "safari", "ie"}, {}},
      {"arch", {"x64", "arm", "riscv"}, {}},
  };
}

/// @brief Find the value index for a given parameter name and value string.
uint32_t ValueIndex(const std::vector<Parameter>& params, const std::string& param_name,
                    const std::string& value) {
  for (size_t i = 0; i < params.size(); ++i) {
    if (params[i].name == param_name) {
      for (size_t j = 0; j < params[i].values.size(); ++j) {
        if (params[i].values[j] == value) {
          return static_cast<uint32_t>(j);
        }
      }
    }
  }
  return kUnassigned;
}

/// @brief Build an assignment vector from name=value pairs.
std::vector<uint32_t> MakeAssignment(
    const std::vector<Parameter>& params,
    const std::vector<std::pair<std::string, std::string>>& pairs) {
  std::vector<uint32_t> assignment(params.size(), kUnassigned);
  for (const auto& [name, value] : pairs) {
    for (size_t i = 0; i < params.size(); ++i) {
      if (params[i].name == name) {
        assignment[i] = ValueIndex(params, name, value);
        break;
      }
    }
  }
  return assignment;
}

}  // namespace

TEST(ConstraintParserTest, SimpleEquals) {
  auto params = MakeParams();
  auto result = ParseConstraint("os=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // os=win should be true when os is win
  auto assignment_win = MakeAssignment(params, {{"os", "win"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_win), ConstraintResult::kTrue);

  // os=win should be false when os is mac
  auto assignment_mac = MakeAssignment(params, {{"os", "mac"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_mac), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, SimpleNotEquals) {
  auto params = MakeParams();
  auto result = ParseConstraint("browser!=ie", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // browser!=ie should be true when browser is chrome
  auto assignment_chrome = MakeAssignment(params, {{"browser", "chrome"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_chrome), ConstraintResult::kTrue);

  // browser!=ie should be false when browser is ie
  auto assignment_ie = MakeAssignment(params, {{"browser", "ie"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_ie), ConstraintResult::kFalse);

  // browser!=ie should be unknown when browser is unassigned
  std::vector<uint32_t> unassigned(params.size(), kUnassigned);
  EXPECT_EQ(result.constraint->Evaluate(unassigned), ConstraintResult::kUnknown);
}

TEST(ConstraintParserTest, IfThenSyntax) {
  auto params = MakeParams();
  auto result = ParseConstraint("IF os=mac THEN browser!=ie", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // os=mac AND browser=ie => false (antecedent true, consequent false)
  auto violating = MakeAssignment(params, {{"os", "mac"}, {"browser", "ie"}});
  EXPECT_EQ(result.constraint->Evaluate(violating), ConstraintResult::kFalse);

  // os=mac AND browser=chrome => true (antecedent true, consequent true)
  auto satisfying = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}});
  EXPECT_EQ(result.constraint->Evaluate(satisfying), ConstraintResult::kTrue);

  // os=win => true (antecedent false, implication vacuously true)
  auto vacuous = MakeAssignment(params, {{"os", "win"}, {"browser", "ie"}});
  EXPECT_EQ(result.constraint->Evaluate(vacuous), ConstraintResult::kTrue);
}

TEST(ConstraintParserTest, ImpliesSyntax) {
  auto params = MakeParams();
  auto result = ParseConstraint("os=linux IMPLIES arch!=arm", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // os=linux AND arch=arm => false
  auto violating = MakeAssignment(params, {{"os", "linux"}, {"arch", "arm"}});
  EXPECT_EQ(result.constraint->Evaluate(violating), ConstraintResult::kFalse);

  // os=linux AND arch=x64 => true
  auto satisfying = MakeAssignment(params, {{"os", "linux"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(satisfying), ConstraintResult::kTrue);

  // os=win AND arch=arm => true (antecedent false)
  auto vacuous = MakeAssignment(params, {{"os", "win"}, {"arch", "arm"}});
  EXPECT_EQ(result.constraint->Evaluate(vacuous), ConstraintResult::kTrue);
}

TEST(ConstraintParserTest, NotExpression) {
  auto params = MakeParams();
  auto result = ParseConstraint("NOT os=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // NOT os=win should be false when os is win
  auto assignment_win = MakeAssignment(params, {{"os", "win"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_win), ConstraintResult::kFalse);

  // NOT os=win should be true when os is mac
  auto assignment_mac = MakeAssignment(params, {{"os", "mac"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_mac), ConstraintResult::kTrue);
}

TEST(ConstraintParserTest, AndOrCombination) {
  auto params = MakeParams();
  auto result = ParseConstraint("os=win AND browser=chrome", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // Both true => true
  auto both_true = MakeAssignment(params, {{"os", "win"}, {"browser", "chrome"}});
  EXPECT_EQ(result.constraint->Evaluate(both_true), ConstraintResult::kTrue);

  // One false => false
  auto one_false = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}});
  EXPECT_EQ(result.constraint->Evaluate(one_false), ConstraintResult::kFalse);

  // Both false => false
  auto both_false = MakeAssignment(params, {{"os", "mac"}, {"browser", "firefox"}});
  EXPECT_EQ(result.constraint->Evaluate(both_false), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, ParenthesizedExpr) {
  auto params = MakeParams();
  auto result = ParseConstraint("NOT (os=win AND browser=safari)", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // os=win AND browser=safari => NOT true => false
  auto inner_true = MakeAssignment(params, {{"os", "win"}, {"browser", "safari"}});
  EXPECT_EQ(result.constraint->Evaluate(inner_true), ConstraintResult::kFalse);

  // os=mac AND browser=safari => NOT false => true
  auto inner_false = MakeAssignment(params, {{"os", "mac"}, {"browser", "safari"}});
  EXPECT_EQ(result.constraint->Evaluate(inner_false), ConstraintResult::kTrue);
}

TEST(ConstraintParserTest, ThreeValuedLogic) {
  auto params = MakeParams();
  auto result = ParseConstraint("os=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // All unassigned => unknown
  std::vector<uint32_t> all_unassigned(params.size(), kUnassigned);
  EXPECT_EQ(result.constraint->Evaluate(all_unassigned), ConstraintResult::kUnknown);
}

TEST(ConstraintParserTest, UnknownParameter) {
  auto params = MakeParams();
  auto result = ParseConstraint("unknown=val", params);
  EXPECT_FALSE(result.error.ok());
  // Error message should mention the unknown parameter
  EXPECT_NE(result.error.message.find("unknown"), std::string::npos);
}

TEST(ConstraintParserTest, UnknownValue) {
  auto params = MakeParams();
  auto result = ParseConstraint("os=unknown", params);
  EXPECT_FALSE(result.error.ok());
  // Error message should mention the unknown value
  EXPECT_NE(result.error.message.find("unknown"), std::string::npos);
}

// ---------------------------------------------------------------------------
// IF/THEN/ELSE
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, IfThenElse) {
  auto params = MakeParams();
  auto result = ParseConstraint("IF os=mac THEN browser!=ie ELSE arch!=arm", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;
  ASSERT_NE(result.constraint, nullptr);

  // os=mac -> browser!=ie must hold
  // os=mac, browser=ie -> kFalse (then-branch violated)
  auto mac_ie = MakeAssignment(params, {{"os", "mac"}, {"browser", "ie"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(mac_ie), ConstraintResult::kFalse);

  // os=mac, browser=chrome -> kTrue (then-branch satisfied)
  auto mac_chrome = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}, {"arch", "arm"}});
  EXPECT_EQ(result.constraint->Evaluate(mac_chrome), ConstraintResult::kTrue);

  // os!=mac -> arch!=arm must hold
  // os=win, arch=arm -> kFalse (else-branch violated)
  auto win_arm = MakeAssignment(params, {{"os", "win"}, {"browser", "chrome"}, {"arch", "arm"}});
  EXPECT_EQ(result.constraint->Evaluate(win_arm), ConstraintResult::kFalse);

  // os=win, arch=x64 -> kTrue (else-branch satisfied)
  auto win_x64 = MakeAssignment(params, {{"os", "win"}, {"browser", "ie"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(win_x64), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Relational operators
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, RelationalGreaterThan) {
  std::vector<Parameter> params = {{"priority", {"1", "2", "3", "4", "5"}, {}}};

  auto result = ParseConstraint("priority > 3", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // priority=4 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kTrue);
  // priority=5 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kTrue);
  // priority=3 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);
  // priority=1 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, RelationalGreaterEqual) {
  std::vector<Parameter> params = {{"priority", {"1", "2", "3", "4", "5"}, {}}};

  auto result = ParseConstraint("priority >= 3", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // priority=3 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);
  // priority=4 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kTrue);
  // priority=5 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kTrue);
  // priority=2 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);
  // priority=1 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, RelationalLessThan) {
  std::vector<Parameter> params = {{"priority", {"1", "2", "3", "4", "5"}, {}}};

  auto result = ParseConstraint("priority < 2", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // priority=1 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  // priority=2 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);
  // priority=3 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, RelationalLessEqual) {
  std::vector<Parameter> params = {{"priority", {"1", "2", "3", "4", "5"}, {}}};

  auto result = ParseConstraint("priority <= 2", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // priority=1 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  // priority=2 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  // priority=3 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);
  // priority=4 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kFalse);
  // priority=5 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// IN operator
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, InOperator) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  auto result = ParseConstraint("env IN {staging, prod}", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // env=staging -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  // env=prod -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);
  // env=dev -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// LIKE operator
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, LikeStarWildcard) {
  std::vector<Parameter> params = {{"browser", {"chrome", "chrome-beta", "firefox", "safari"}, {}}};

  auto result = ParseConstraint("browser LIKE chrome*", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // browser=chrome -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  // browser=chrome-beta -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  // browser=firefox -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);
  // browser=safari -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, LikeQuestionWildcard) {
  std::vector<Parameter> params = {{"size", {"s", "m", "l", "xl", "xxl"}, {}}};

  auto result = ParseConstraint("size LIKE ?l", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // size=xl -> kTrue (one char + "l")
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kTrue);
  // size=s -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
  // size=l -> kFalse (too short, "?" requires exactly one char)
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);
  // size=xxl -> kFalse (too long for ?l)
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// Parameter-to-parameter comparison
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, ParamToParamRelational) {
  std::vector<Parameter> params = {
      {"start", {"1", "2", "3"}, {}},
      {"end", {"1", "2", "3"}, {}},
  };

  auto result = ParseConstraint("start < end", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // start=1, end=2 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0, 1}), ConstraintResult::kTrue);
  // start=1, end=3 -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0, 2}), ConstraintResult::kTrue);
  // start=2, end=2 -> kFalse (not strictly less)
  EXPECT_EQ(result.constraint->Evaluate({1, 1}), ConstraintResult::kFalse);
  // start=3, end=1 -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({2, 0}), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, ParamToParamEquals) {
  std::vector<Parameter> params = {
      {"src", {"a", "b", "c"}, {}},
      {"dst", {"a", "b", "c"}, {}},
  };

  auto result = ParseConstraint("src = dst", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // src=a, dst=a -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0, 0}), ConstraintResult::kTrue);
  // src=a, dst=b -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({0, 1}), ConstraintResult::kFalse);
}

TEST(ConstraintParserTest, ParamToParamNotEquals) {
  std::vector<Parameter> params = {
      {"src", {"a", "b", "c"}, {}},
      {"dst", {"a", "b", "c"}, {}},
  };

  auto result = ParseConstraint("src != dst", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // src=a, dst=a -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({0, 0}), ConstraintResult::kFalse);
  // src=a, dst=b -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0, 1}), ConstraintResult::kTrue);
  // src=b, dst=c -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({1, 2}), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Case-insensitive (default)
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, CaseInsensitiveDefault) {
  std::vector<Parameter> params = {{"os", {"win", "mac"}, {}},
                                   {"browser", {"chrome", "firefox"}, {}}};

  // Mixed-case in the expression; default is case-insensitive.
  auto result = ParseConstraint("OS=Win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // os=win -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({0, kUnassigned}), ConstraintResult::kTrue);
  // os=mac -> kFalse
  EXPECT_EQ(result.constraint->Evaluate({1, kUnassigned}), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// Case-sensitive
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, CaseSensitiveFails) {
  std::vector<Parameter> params = {{"os", {"win", "mac"}, {}}};
  coverwise::model::ParseOptions opts{.case_sensitive = true};

  // "OS" does not match "os" in case-sensitive mode.
  auto result = ParseConstraint("OS=Win", params, opts);
  EXPECT_FALSE(result.error.ok());
}

TEST(ConstraintParserTest, CaseSensitiveExactMatch) {
  std::vector<Parameter> params = {{"os", {"win", "mac"}, {}}};
  coverwise::model::ParseOptions opts{.case_sensitive = true};

  // Exact case matches succeed.
  auto result = ParseConstraint("os=win", params, opts);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// Unconditional constraint (standalone expression, no IF)
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, UnconditionalConstraint) {
  std::vector<Parameter> params = {{"priority", {"1", "2", "3", "4", "5"}, {}}};

  // "priority > 3" without IF — acts as invariant.
  auto result = ParseConstraint("priority > 3", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // Every test case must satisfy this.
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);  // 1
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);  // 2
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);  // 3
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kTrue);   // 4
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kTrue);   // 5
}

// ---------------------------------------------------------------------------
// Complex combined expression
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, ComplexIfRelationalIn) {
  std::vector<Parameter> params = {
      {"priority", {"1", "2", "3", "4", "5"}, {}},
      {"env", {"dev", "staging", "prod"}, {}},
  };

  auto result = ParseConstraint("IF priority > 3 THEN env IN {staging, prod}", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // priority=4, env=staging -> kTrue (condition true, consequent true)
  EXPECT_EQ(result.constraint->Evaluate({3, 1}), ConstraintResult::kTrue);
  // priority=5, env=prod -> kTrue
  EXPECT_EQ(result.constraint->Evaluate({4, 2}), ConstraintResult::kTrue);
  // priority=4, env=dev -> kFalse (condition true, consequent false)
  EXPECT_EQ(result.constraint->Evaluate({3, 0}), ConstraintResult::kFalse);
  // priority=2, env=dev -> kTrue (condition false, implication vacuously true)
  EXPECT_EQ(result.constraint->Evaluate({1, 0}), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Three-valued logic for new node types
// ---------------------------------------------------------------------------

TEST(ConstraintParserTest, RelationalUnassignedIsUnknown) {
  std::vector<Parameter> params = {{"priority", {"1", "2", "3"}, {}}};

  auto result = ParseConstraint("priority > 1", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // Unassigned -> kUnknown
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned}), ConstraintResult::kUnknown);
}

TEST(ConstraintParserTest, InUnassignedIsUnknown) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  auto result = ParseConstraint("env IN {staging, prod}", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // Unassigned -> kUnknown
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned}), ConstraintResult::kUnknown);
}

TEST(ConstraintParserTest, LikeUnassignedIsUnknown) {
  std::vector<Parameter> params = {{"browser", {"chrome", "firefox"}, {}}};

  auto result = ParseConstraint("browser LIKE chrome*", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // Unassigned -> kUnknown
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned}), ConstraintResult::kUnknown);
}

TEST(ConstraintParserTest, IfThenElseConditionUnknown) {
  auto params = MakeParams();
  auto result = ParseConstraint("IF os=mac THEN browser!=ie ELSE arch!=arm", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // os unassigned, browser=ie, arch=arm -> condition unknown
  // then-branch: browser=ie => kFalse, else-branch: arch=arm => kFalse
  // Both branches agree on kFalse -> kFalse
  auto both_false = MakeAssignment(params, {{"browser", "ie"}, {"arch", "arm"}});
  EXPECT_EQ(result.constraint->Evaluate(both_false), ConstraintResult::kFalse);

  // os unassigned, browser=chrome, arch=x64 -> condition unknown
  // then-branch: browser!=ie => kTrue, else-branch: arch!=arm => kTrue
  // Both branches agree on kTrue -> kTrue
  auto both_true = MakeAssignment(params, {{"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(both_true), ConstraintResult::kTrue);

  // os unassigned, browser=ie, arch=x64 -> condition unknown
  // then-branch: browser!=ie => kFalse, else-branch: arch!=arm => kTrue
  // Branches disagree -> kUnknown
  auto disagree = MakeAssignment(params, {{"browser", "ie"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(disagree), ConstraintResult::kUnknown);
}

TEST(ConstraintParserTest, ParamToParamRelationalUnassigned) {
  std::vector<Parameter> params = {
      {"start", {"1", "2", "3"}, {}},
      {"end", {"1", "2", "3"}, {}},
  };

  auto result = ParseConstraint("start < end", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // start assigned, end unassigned -> kUnknown
  EXPECT_EQ(result.constraint->Evaluate({0, kUnassigned}), ConstraintResult::kUnknown);

  // start unassigned, end assigned -> kUnknown
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned, 1}), ConstraintResult::kUnknown);

  // Both unassigned -> kUnknown
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned, kUnassigned}), ConstraintResult::kUnknown);
}

// ===========================================================================
// Edge case tests (defensive)
// ===========================================================================

// ---------------------------------------------------------------------------
// Reserved word collisions
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, ReservedWordSubstringInParamName) {
  // Parameter named "if_condition" should NOT be parsed as keyword IF.
  // The tokenizer reads full identifier tokens, so "if_condition" is one token.
  std::vector<Parameter> params = {
      {"if_condition", {"yes", "no"}, {}},
      {"order", {"first", "second"}, {}},
  };

  auto result = ParseConstraint("if_condition=yes", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0, kUnassigned}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({1, kUnassigned}), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, ReservedWordSubstringInValue) {
  // Value "not_available" should NOT parse "not" as NOT keyword.
  std::vector<Parameter> params = {
      {"status", {"not_available", "available", "pending"}, {}},
  };

  auto result = ParseConstraint("status=not_available", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, ValueContainingAndSubstring) {
  // Value "android" should not match "and" keyword.
  std::vector<Parameter> params = {
      {"platform", {"android", "ios", "web"}, {}},
  };

  auto result = ParseConstraint("platform=android", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, ParamNamedImplies) {
  // Parameter named "implies_flag" should not collide with IMPLIES keyword.
  std::vector<Parameter> params = {
      {"implies_flag", {"on", "off"}, {}},
      {"mode", {"fast", "slow"}, {}},
  };

  auto result = ParseConstraint("implies_flag=on", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0, kUnassigned}), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Single-value parameter
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, SingleValueParam) {
  // A parameter with exactly one possible value.
  std::vector<Parameter> params = {
      {"fixed", {"only"}, {}},
      {"mode", {"a", "b"}, {}},
  };

  // Constraint "fixed=only" is trivially true for all tests.
  auto result = ParseConstraint("fixed=only", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0, 0}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({0, 1}), ConstraintResult::kTrue);
}

TEST(ConstraintParserEdgeCaseTest, SingleValueParamNotEquals) {
  std::vector<Parameter> params = {
      {"fixed", {"only"}, {}},
      {"mode", {"a", "b"}, {}},
  };

  // Constraint "fixed!=only" is trivially false for the only possible value.
  auto result = ParseConstraint("fixed!=only", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0, 0}), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// Negative numbers
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, NegativeNumberRelational) {
  std::vector<Parameter> params = {
      {"temp", {"-10", "-1", "0", "1", "10"}, {}},
  };

  // temp > -5 => values 0, 1, 10 satisfy (indices 2, 3, 4)
  auto result = ParseConstraint("temp > -5", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // "-10" = -10 > -5? No => kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
  // "-1" = -1 > -5? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  // "0" = 0 > -5? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);
  // "1" = 1 > -5? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kTrue);
  // "10" = 10 > -5? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kTrue);
}

TEST(ConstraintParserEdgeCaseTest, NegativeNumberGreaterEqual) {
  std::vector<Parameter> params = {
      {"temp", {"-10", "-1", "0", "1", "10"}, {}},
  };

  // temp >= -1 => values -1, 0, 1, 10 satisfy (indices 1, 2, 3, 4)
  auto result = ParseConstraint("temp >= -1", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // "-10" >= -1? No => kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
  // "-1" >= -1? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  // "0" >= -1? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);
  // "1" >= -1? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({3}), ConstraintResult::kTrue);
  // "10" >= -1? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({4}), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Whitespace in braces (IN operator)
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, InWithExtraSpaces) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  // Extra spaces around values in braces.
  auto result = ParseConstraint("env IN { staging , prod }", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);  // dev
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);   // staging
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);   // prod
}

TEST(ConstraintParserEdgeCaseTest, InWithNoSpaces) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  // No spaces around values in braces.
  auto result = ParseConstraint("env IN {staging,prod}", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);  // dev
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);   // staging
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);   // prod
}

// ---------------------------------------------------------------------------
// Empty IN set
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, EmptyInSet) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  // Empty set should produce a parse error.
  auto result = ParseConstraint("env IN {}", params);
  EXPECT_FALSE(result.error.ok());
}

// ---------------------------------------------------------------------------
// Multiple NOT (double negation)
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, DoubleNegation) {
  auto params = MakeParams();

  // NOT NOT os=win is equivalent to os=win.
  auto result = ParseConstraint("NOT NOT os=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto assignment_win = MakeAssignment(params, {{"os", "win"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_win), ConstraintResult::kTrue);

  auto assignment_mac = MakeAssignment(params, {{"os", "mac"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_mac), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, TripleNegation) {
  auto params = MakeParams();

  // NOT NOT NOT os=win is equivalent to NOT os=win.
  auto result = ParseConstraint("NOT NOT NOT os=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto assignment_win = MakeAssignment(params, {{"os", "win"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_win), ConstraintResult::kFalse);

  auto assignment_mac = MakeAssignment(params, {{"os", "mac"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_mac), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Deeply nested parentheses
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, DeeplyNestedParens) {
  auto params = MakeParams();

  auto result = ParseConstraint("(((os=win)))", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto assignment_win = MakeAssignment(params, {{"os", "win"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_win), ConstraintResult::kTrue);

  auto assignment_mac = MakeAssignment(params, {{"os", "mac"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_mac), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, NestedParensWithOperators) {
  auto params = MakeParams();

  auto result = ParseConstraint("((os=win) AND (browser=chrome))", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto both = MakeAssignment(params, {{"os", "win"}, {"browser", "chrome"}});
  EXPECT_EQ(result.constraint->Evaluate(both), ConstraintResult::kTrue);

  auto one_wrong = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}});
  EXPECT_EQ(result.constraint->Evaluate(one_wrong), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// Unicode / multibyte parameter names
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, JapaneseParameterNames) {
  std::vector<Parameter> params = {
      {"OS", {"Windows", "macOS"}, {}},
      {"\xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6",  // "ブラウザ" (browser)
       {"Chrome", "Safari"}},
  };

  // Constraint using Japanese parameter name: ブラウザ=Chrome
  auto result = ParseConstraint("\xE3\x83\x96\xE3\x83\xA9\xE3\x82\xA6\xE3\x82\xB6=Chrome", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // ブラウザ=Chrome => kTrue
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned, 0}), ConstraintResult::kTrue);
  // ブラウザ=Safari => kFalse
  EXPECT_EQ(result.constraint->Evaluate({kUnassigned, 1}), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, JapaneseParameterAndValues) {
  std::vector<Parameter> params = {
      {"\xE7\x92\xB0\xE5\xA2\x83",                                 // "環境"
       {"\xE9\x96\x8B\xE7\x99\xBA", "\xE6\x9C\xAC\xE7\x95\xAA"}},  // "開発", "本番"
  };

  // 環境=本番
  auto result = ParseConstraint("\xE7\x92\xB0\xE5\xA2\x83=\xE6\x9C\xAC\xE7\x95\xAA", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // 環境=開発 => kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
  // 環境=本番 => kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
}

TEST(ConstraintParserEdgeCaseTest, JapaneseWithLogicalOperators) {
  std::vector<Parameter> params = {
      {"\xE7\x92\xB0\xE5\xA2\x83",                                 // "環境"
       {"\xE9\x96\x8B\xE7\x99\xBA", "\xE6\x9C\xAC\xE7\x95\xAA"}},  // "開発", "本番"
      {"region", {"us", "jp"}, {}},
  };

  // IF 環境=本番 THEN region=jp
  auto result = ParseConstraint(
      "IF \xE7\x92\xB0\xE5\xA2\x83=\xE6\x9C\xAC\xE7\x95\xAA THEN region=jp", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // 環境=本番, region=jp => kTrue (antecedent true, consequent true)
  EXPECT_EQ(result.constraint->Evaluate({1, 1}), ConstraintResult::kTrue);
  // 環境=本番, region=us => kFalse (antecedent true, consequent false)
  EXPECT_EQ(result.constraint->Evaluate({1, 0}), ConstraintResult::kFalse);
  // 環境=開発, region=us => kTrue (antecedent false, vacuously true)
  EXPECT_EQ(result.constraint->Evaluate({0, 0}), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Dot in identifiers (version strings)
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, DotInValues) {
  std::vector<Parameter> params = {
      {"version", {"1.0", "2.5", "3.14"}, {}},
  };

  // version=3.14 should parse the dot as part of the identifier.
  auto result = ParseConstraint("version=3.14", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);  // 1.0
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);  // 2.5
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);   // 3.14
}

// ---------------------------------------------------------------------------
// Type mismatch in relational operators
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, RelationalOnNonNumericValues) {
  // Non-numeric values with relational operator: should evaluate to kFalse.
  std::vector<Parameter> params = {
      {"name", {"alice", "bob", "charlie"}, {}},
  };

  auto result = ParseConstraint("name > 5", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // Non-numeric values compared with > should all be kFalse.
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);  // "alice"
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);  // "bob"
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kFalse);  // "charlie"
}

// ---------------------------------------------------------------------------
// Contradictory constraints
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, ContradictoryConstraint) {
  auto params = MakeParams();

  // os=win AND os!=win is always false (self-contradictory).
  auto result = ParseConstraint("os=win AND os!=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // Every assignment should evaluate to kFalse.
  auto win = MakeAssignment(params, {{"os", "win"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(win), ConstraintResult::kFalse);

  auto mac = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(mac), ConstraintResult::kFalse);

  auto linux_a = MakeAssignment(params, {{"os", "linux"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(linux_a), ConstraintResult::kFalse);
}

// ---------------------------------------------------------------------------
// Tautology constraints
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, TautologyOrConstraint) {
  auto params = MakeParams();

  // os=win OR os!=win is always true (tautology).
  auto result = ParseConstraint("os=win OR os!=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto win = MakeAssignment(params, {{"os", "win"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(win), ConstraintResult::kTrue);

  auto mac = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(mac), ConstraintResult::kTrue);

  auto linux_a = MakeAssignment(params, {{"os", "linux"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(linux_a), ConstraintResult::kTrue);
}

TEST(ConstraintParserEdgeCaseTest, TautologyImplies) {
  auto params = MakeParams();

  // os=win IMPLIES os=win is always true.
  auto result = ParseConstraint("os=win IMPLIES os=win", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto win = MakeAssignment(params, {{"os", "win"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(win), ConstraintResult::kTrue);

  auto mac = MakeAssignment(params, {{"os", "mac"}, {"browser", "chrome"}, {"arch", "x64"}});
  EXPECT_EQ(result.constraint->Evaluate(mac), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Special characters in values (via dot support)
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, VersionStringComparison) {
  // Values with dots should tokenize correctly.
  std::vector<Parameter> params = {
      {"version", {"1.0", "2.0", "3.0"}, {}},
  };

  auto result = ParseConstraint("version > 1.5", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  // "1.0" > 1.5? No => kFalse
  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);
  // "2.0" > 1.5? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  // "3.0" > 1.5? Yes => kTrue
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);
}

// ---------------------------------------------------------------------------
// Error cases
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, EmptyExpression) {
  auto params = MakeParams();
  auto result = ParseConstraint("", params);
  EXPECT_FALSE(result.error.ok());
}

TEST(ConstraintParserEdgeCaseTest, MismatchedParentheses) {
  auto params = MakeParams();

  auto result1 = ParseConstraint("(os=win", params);
  EXPECT_FALSE(result1.error.ok());

  auto result2 = ParseConstraint("os=win)", params);
  EXPECT_FALSE(result2.error.ok());
}

TEST(ConstraintParserEdgeCaseTest, IfWithoutThen) {
  auto params = MakeParams();
  auto result = ParseConstraint("IF os=win os=mac", params);
  EXPECT_FALSE(result.error.ok());
}

TEST(ConstraintParserEdgeCaseTest, DanglingOperator) {
  auto params = MakeParams();

  auto result = ParseConstraint("os=win AND", params);
  EXPECT_FALSE(result.error.ok());
}

TEST(ConstraintParserEdgeCaseTest, DoubleOperator) {
  auto params = MakeParams();

  auto result = ParseConstraint("os=win AND AND browser=chrome", params);
  EXPECT_FALSE(result.error.ok());
}

// ---------------------------------------------------------------------------
// Complex combined edge cases
// ---------------------------------------------------------------------------

TEST(ConstraintParserEdgeCaseTest, NestedNotWithParensAndLogical) {
  auto params = MakeParams();

  // NOT (NOT os=win) is equivalent to os=win.
  auto result = ParseConstraint("NOT (NOT os=win)", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  auto assignment_win = MakeAssignment(params, {{"os", "win"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_win), ConstraintResult::kTrue);

  auto assignment_mac = MakeAssignment(params, {{"os", "mac"}});
  EXPECT_EQ(result.constraint->Evaluate(assignment_mac), ConstraintResult::kFalse);
}

TEST(ConstraintParserEdgeCaseTest, InWithSingleValue) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  // IN with just one value is like =.
  auto result = ParseConstraint("env IN {prod}", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kFalse);  // dev
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kFalse);  // staging
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);   // prod
}

TEST(ConstraintParserEdgeCaseTest, InWithAllValues) {
  std::vector<Parameter> params = {{"env", {"dev", "staging", "prod"}, {}}};

  // IN with all values is always true.
  auto result = ParseConstraint("env IN {dev, staging, prod}", params);
  ASSERT_TRUE(result.error.ok()) << result.error.message;

  EXPECT_EQ(result.constraint->Evaluate({0}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({1}), ConstraintResult::kTrue);
  EXPECT_EQ(result.constraint->Evaluate({2}), ConstraintResult::kTrue);
}
