/// @file constraint_ast.h
/// @brief AST-based constraint representation for combinatorial test generation.

#ifndef COVERWISE_MODEL_CONSTRAINT_AST_H_
#define COVERWISE_MODEL_CONSTRAINT_AST_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "util/string_util.h"

namespace coverwise {
namespace model {

using util::IsNumeric;
using util::ToDouble;

/// @brief Sentinel value indicating an unassigned parameter.
constexpr uint32_t kUnassigned = UINT32_MAX;

/// @brief Result of evaluating a constraint against a partial assignment.
enum class ConstraintResult {
  kTrue,     ///< Constraint is satisfied
  kFalse,    ///< Constraint is violated
  kUnknown,  ///< Cannot determine (unassigned parameters)
};

/// @brief Base class for constraint AST nodes.
class ConstraintNode {
 public:
  virtual ~ConstraintNode() = default;

  /// @brief Evaluate this constraint against a (possibly partial) assignment.
  /// @param assignment Value indices per parameter. kUnassigned = unassigned.
  virtual ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const = 0;
};

/// @brief A constraint expression (owning pointer to root node).
using Constraint = std::unique_ptr<ConstraintNode>;

/// @brief Equality comparison: param_index == value_index.
class EqualsNode : public ConstraintNode {
 public:
  /// @param param_index Index of the parameter.
  /// @param value_index Index of the value within that parameter.
  EqualsNode(uint32_t param_index, uint32_t value_index);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

  uint32_t param_index() const { return param_index_; }
  uint32_t value_index() const { return value_index_; }

 private:
  uint32_t param_index_;
  uint32_t value_index_;
};

/// @brief Inequality comparison: param_index != value_index.
class NotEqualsNode : public ConstraintNode {
 public:
  /// @param param_index Index of the parameter.
  /// @param value_index Index of the value within that parameter.
  NotEqualsNode(uint32_t param_index, uint32_t value_index);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

  uint32_t param_index() const { return param_index_; }
  uint32_t value_index() const { return value_index_; }

 private:
  uint32_t param_index_;
  uint32_t value_index_;
};

/// @brief Logical AND of two sub-expressions.
class AndNode : public ConstraintNode {
 public:
  /// @param left Left operand (takes ownership).
  /// @param right Right operand (takes ownership).
  AndNode(Constraint left, Constraint right);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  Constraint left_;
  Constraint right_;
};

/// @brief Logical OR of two sub-expressions.
class OrNode : public ConstraintNode {
 public:
  /// @param left Left operand (takes ownership).
  /// @param right Right operand (takes ownership).
  OrNode(Constraint left, Constraint right);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  Constraint left_;
  Constraint right_;
};

/// @brief Logical NOT of a sub-expression.
class NotNode : public ConstraintNode {
 public:
  /// @param child Child operand (takes ownership).
  explicit NotNode(Constraint child);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  Constraint child_;
};

/// @brief Logical implication: left IMPLIES right (= NOT left OR right).
class ImpliesNode : public ConstraintNode {
 public:
  /// @param antecedent The "if" part (takes ownership).
  /// @param consequent The "then" part (takes ownership).
  ImpliesNode(Constraint antecedent, Constraint consequent);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  Constraint antecedent_;
  Constraint consequent_;
};

/// @brief IF/THEN/ELSE ternary constraint.
///
/// Three-valued evaluation:
/// - condition=kTrue  -> evaluate then_branch
/// - condition=kFalse -> evaluate else_branch
/// - condition=kUnknown -> if both branches agree, use that; else kUnknown
class IfThenElseNode : public ConstraintNode {
 public:
  /// @param condition The condition expression (takes ownership).
  /// @param then_branch Evaluated when condition is true (takes ownership).
  /// @param else_branch Evaluated when condition is false (takes ownership).
  IfThenElseNode(Constraint condition, Constraint then_branch, Constraint else_branch);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  Constraint condition_;
  Constraint then_branch_;
  Constraint else_branch_;
};

/// @brief Relational comparison operators.
enum class RelOp { kLess, kLessEqual, kGreater, kGreaterEqual };

/// @brief Relational comparison of a parameter's numeric value against a literal or another param.
///
/// Compares parameter values as doubles. If a value cannot be parsed as numeric,
/// the result is kFalse.
class RelationalNode : public ConstraintNode {
 public:
  /// @brief Compare a parameter value against a literal numeric value.
  /// @param param_index Index of the parameter.
  /// @param op Relational operator.
  /// @param literal The numeric literal to compare against.
  /// @param param_values The string values of the parameter (copied for numeric conversion).
  RelationalNode(uint32_t param_index, RelOp op, double literal,
                 const std::vector<std::string>& param_values);

  /// @brief Compare two parameter values against each other.
  /// @param left_param Index of the left parameter.
  /// @param op Relational operator.
  /// @param right_param Index of the right parameter.
  /// @param left_values The string values of the left parameter.
  /// @param right_values The string values of the right parameter.
  RelationalNode(uint32_t left_param, RelOp op, uint32_t right_param,
                 const std::vector<std::string>& left_values,
                 const std::vector<std::string>& right_values);

  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  bool CompareValues(double left, double right) const;

  uint32_t left_param_;
  RelOp op_;
  bool is_param_comparison_;
  double literal_;
  uint32_t right_param_;
  std::vector<std::string> left_values_;
  std::vector<std::string> right_values_;
};

/// @brief IN-set membership test: param IN {val1, val2, ...}.
///
/// Three-valued: unassigned -> kUnknown, value in set -> kTrue, else kFalse.
class InNode : public ConstraintNode {
 public:
  /// @param param_index Index of the parameter.
  /// @param value_indices Indices of the values that form the set.
  InNode(uint32_t param_index, std::vector<uint32_t> value_indices);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  uint32_t param_index_;
  std::vector<uint32_t> value_indices_;
};

/// @brief LIKE pattern matching: param LIKE pattern.
///
/// Supports `*` (any string) and `?` (single character) wildcards.
/// The pattern is matched against the string value of the parameter.
/// Matching results are precomputed at construction time for efficiency.
class LikeNode : public ConstraintNode {
 public:
  /// @param param_index Index of the parameter.
  /// @param pattern The glob-like pattern string.
  /// @param param_values The string values of the parameter (used to precompute matches).
  LikeNode(uint32_t param_index, const std::string& pattern,
           const std::vector<std::string>& param_values);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

  /// @brief Test whether a string matches a glob pattern (* and ?).
  /// @param pattern The pattern to match against.
  /// @param text The text to test.
  /// @return true if the text matches the pattern.
  static bool GlobMatch(const std::string& pattern, const std::string& text);

 private:
  uint32_t param_index_;
  std::string pattern_;
  std::vector<bool> matches_;
};

/// @brief Parameter-to-parameter equality comparison.
///
/// Compares the string values of two parameters. Equal if the string
/// representations are identical.
class ParamEqualsNode : public ConstraintNode {
 public:
  /// @param left_param Index of the left parameter.
  /// @param right_param Index of the right parameter.
  /// @param left_values String values of the left parameter.
  /// @param right_values String values of the right parameter.
  ParamEqualsNode(uint32_t left_param, uint32_t right_param,
                  const std::vector<std::string>& left_values,
                  const std::vector<std::string>& right_values);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  uint32_t left_param_;
  uint32_t right_param_;
  std::vector<std::string> left_values_;
  std::vector<std::string> right_values_;
};

/// @brief Parameter-to-parameter inequality comparison.
///
/// Compares the string values of two parameters. Not equal if the string
/// representations differ.
class ParamNotEqualsNode : public ConstraintNode {
 public:
  /// @param left_param Index of the left parameter.
  /// @param right_param Index of the right parameter.
  /// @param left_values String values of the left parameter.
  /// @param right_values String values of the right parameter.
  ParamNotEqualsNode(uint32_t left_param, uint32_t right_param,
                     const std::vector<std::string>& left_values,
                     const std::vector<std::string>& right_values);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  uint32_t left_param_;
  uint32_t right_param_;
  std::vector<std::string> left_values_;
  std::vector<std::string> right_values_;
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_CONSTRAINT_AST_H_
