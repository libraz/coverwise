#include "validator/constraint_validator.h"

#include <gtest/gtest.h>

#include <memory>

#include "model/constraint_ast.h"
#include "model/test_case.h"

using coverwise::model::AndNode;
using coverwise::model::Constraint;
using coverwise::model::EqualsNode;
using coverwise::model::ImpliesNode;
using coverwise::model::NotEqualsNode;
using coverwise::model::NotNode;
using coverwise::model::OrNode;
using coverwise::model::TestCase;
using coverwise::validator::ConstraintReport;
using coverwise::validator::ValidateConstraints;

// Helper: make a TestCase from a list of value indices.
static TestCase MakeTest(std::vector<uint32_t> values) { return TestCase{std::move(values)}; }

TEST(ConstraintValidatorTest, EmptyTestsEmptyConstraints) {
  std::vector<TestCase> tests;
  std::vector<Constraint> constraints;

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 0u);
  EXPECT_EQ(report.violations, 0u);
  EXPECT_TRUE(report.violating_indices.empty());
}

TEST(ConstraintValidatorTest, TestsWithNoConstraints) {
  std::vector<TestCase> tests = {MakeTest({0, 1}), MakeTest({1, 0})};
  std::vector<Constraint> constraints;

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 2u);
  EXPECT_EQ(report.violations, 0u);
  EXPECT_TRUE(report.violating_indices.empty());
}

TEST(ConstraintValidatorTest, NoTestsWithConstraints) {
  std::vector<TestCase> tests;
  std::vector<Constraint> constraints;
  // p0 == 0
  constraints.push_back(std::make_unique<EqualsNode>(0, 0));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 0u);
  EXPECT_EQ(report.violations, 0u);
  EXPECT_TRUE(report.violating_indices.empty());
}

TEST(ConstraintValidatorTest, AllTestsSatisfyConstraint) {
  // Constraint: p0 != 1 (i.e. p0 must not be value index 1)
  // All tests have p0 = 0, so no violations.
  std::vector<TestCase> tests = {MakeTest({0, 0}), MakeTest({0, 1}), MakeTest({0, 2})};
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 1));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 0u);
  EXPECT_TRUE(report.violating_indices.empty());
}

TEST(ConstraintValidatorTest, OneTestViolates) {
  // Constraint: p0 != 1
  // Test at index 1 has p0 = 1 -> violation.
  std::vector<TestCase> tests = {MakeTest({0, 0}), MakeTest({1, 0}), MakeTest({0, 1})};
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 1));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 1u);
  ASSERT_EQ(report.violating_indices.size(), 1u);
  EXPECT_EQ(report.violating_indices[0], 1u);
}

TEST(ConstraintValidatorTest, MultipleTestsViolate) {
  // Constraint: p0 != 1
  // Tests at indices 0 and 2 have p0 = 1 -> violations.
  std::vector<TestCase> tests = {MakeTest({1, 0}), MakeTest({0, 0}), MakeTest({1, 1})};
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 1));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 2u);
  ASSERT_EQ(report.violating_indices.size(), 2u);
  EXPECT_EQ(report.violating_indices[0], 0u);
  EXPECT_EQ(report.violating_indices[1], 2u);
}

TEST(ConstraintValidatorTest, MultipleConstraintsOneViolated) {
  // Constraint 1: p0 != 2  (all tests satisfy this)
  // Constraint 2: p1 != 1  (test at index 1 violates this)
  std::vector<TestCase> tests = {MakeTest({0, 0}), MakeTest({0, 1}), MakeTest({1, 0})};
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<NotEqualsNode>(0, 2));
  constraints.push_back(std::make_unique<NotEqualsNode>(1, 1));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 1u);
  ASSERT_EQ(report.violating_indices.size(), 1u);
  EXPECT_EQ(report.violating_indices[0], 1u);
}

TEST(ConstraintValidatorTest, ComplexAndOrConstraint) {
  // Constraint: (p0 == 0) AND (p1 == 0 OR p1 == 1)
  // Test {0, 0} -> (true) AND (true OR false) -> true
  // Test {0, 2} -> (true) AND (false OR false) -> false  (violation)
  // Test {1, 0} -> (false) AND (...) -> false  (violation)
  std::vector<TestCase> tests = {MakeTest({0, 0}), MakeTest({0, 2}), MakeTest({1, 0})};
  std::vector<Constraint> constraints;
  constraints.push_back(
      std::make_unique<AndNode>(std::make_unique<EqualsNode>(0, 0),
                                std::make_unique<OrNode>(std::make_unique<EqualsNode>(1, 0),
                                                         std::make_unique<EqualsNode>(1, 1))));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 2u);
  ASSERT_EQ(report.violating_indices.size(), 2u);
  EXPECT_EQ(report.violating_indices[0], 1u);
  EXPECT_EQ(report.violating_indices[1], 2u);
}

TEST(ConstraintValidatorTest, ImpliesNodeViolation) {
  // Constraint: IF p0 == 0 THEN p1 != 0
  // Equivalently: NOT(p0 == 0) OR (p1 != 0)
  //
  // Test {0, 0} -> antecedent true, consequent false -> violation
  // Test {0, 1} -> antecedent true, consequent true  -> ok
  // Test {1, 0} -> antecedent false -> ok (implication vacuously true)
  std::vector<TestCase> tests = {MakeTest({0, 0}), MakeTest({0, 1}), MakeTest({1, 0})};
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<ImpliesNode>(std::make_unique<EqualsNode>(0, 0),
                                                      std::make_unique<NotEqualsNode>(1, 0)));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 1u);
  ASSERT_EQ(report.violating_indices.size(), 1u);
  EXPECT_EQ(report.violating_indices[0], 0u);
}

TEST(ConstraintValidatorTest, AllTestsViolate) {
  // Constraint: p0 == 0
  // All tests have p0 = 1 -> all violate.
  std::vector<TestCase> tests = {MakeTest({1, 0}), MakeTest({1, 1}), MakeTest({1, 2})};
  std::vector<Constraint> constraints;
  constraints.push_back(std::make_unique<EqualsNode>(0, 0));

  auto report = ValidateConstraints(tests, constraints);
  EXPECT_EQ(report.total_tests, 3u);
  EXPECT_EQ(report.violations, 3u);
  ASSERT_EQ(report.violating_indices.size(), 3u);
  EXPECT_EQ(report.violating_indices[0], 0u);
  EXPECT_EQ(report.violating_indices[1], 1u);
  EXPECT_EQ(report.violating_indices[2], 2u);
}

// Edge: constraint with kUnknown result (partial assignment)
TEST(ConstraintValidatorTest, PartialAssignmentNoViolation) {
  // Test case with kUnassigned value
  std::vector<TestCase> tests = {MakeTest({0, coverwise::model::kUnassigned})};
  std::vector<Constraint> constraints;
  // IF param0=0 THEN param1!=0
  constraints.push_back(std::make_unique<ImpliesNode>(std::make_unique<EqualsNode>(0, 0),
                                                      std::make_unique<NotEqualsNode>(1, 0)));
  auto report = ValidateConstraints(tests, constraints);
  // kUnknown should NOT count as violation
  EXPECT_EQ(report.violations, 0u);
}
