/// @file constraint_ast.h
/// @brief AST-based constraint representation for combinatorial test generation.

#ifndef COVERWISE_MODEL_CONSTRAINT_AST_H_
#define COVERWISE_MODEL_CONSTRAINT_AST_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
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

/// @brief Immutable numeric parsing cache shared by relational atoms.
struct NumericValueCache {
  std::vector<double> numeric;
  std::vector<uint8_t> valid;
};

using NumericValueCachePtr = std::shared_ptr<const NumericValueCache>;

/// @brief Build a numeric cache for one parameter's value strings.
NumericValueCachePtr BuildNumericValueCache(const std::vector<std::string>& values);

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
  RelationalNode(uint32_t param_index, RelOp op, double literal, NumericValueCachePtr param_cache);

  /// @brief Compare two parameter values against each other.
  /// @param left_param Index of the left parameter.
  /// @param op Relational operator.
  /// @param right_param Index of the right parameter.
  /// @param left_values The string values of the left parameter.
  /// @param right_values The string values of the right parameter.
  RelationalNode(uint32_t left_param, RelOp op, uint32_t right_param,
                 const std::vector<std::string>& left_values,
                 const std::vector<std::string>& right_values);
  RelationalNode(uint32_t left_param, RelOp op, uint32_t right_param,
                 NumericValueCachePtr left_cache, NumericValueCachePtr right_cache);

  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  bool CompareValues(double left, double right) const;

  uint32_t left_param_;
  RelOp op_;
  bool is_param_comparison_;
  double literal_;
  uint32_t right_param_;
  // Numeric conversions are precomputed at construction so Evaluate never
  // reparses on the hot path (mirrors LikeNode's precomputed matching). For
  // each value index, `*_valid_` records whether the string is numeric and
  // `*_numeric_` holds its parsed double (0.0 when not numeric).
  NumericValueCachePtr left_cache_;
  NumericValueCachePtr right_cache_;
};

/// @brief IN-set membership test: param IN {val1, val2, ...}.
///
/// Three-valued: unassigned -> kUnknown, value in set -> kTrue, else kFalse.
/// Membership is precomputed at construction time, so evaluation costs the same
/// whatever the size of the set.
class InNode : public ConstraintNode {
 public:
  /// @param param_index Index of the parameter.
  /// @param value_indices Indices of the values that form the set.
  InNode(uint32_t param_index, std::vector<uint32_t> value_indices);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  uint32_t param_index_;
  // Membership by value index, the same way LikeNode precomputes its matches.
  // Sized to the largest index the set actually holds, and never past the
  // largest a parameter may have: one bit per value, so the whole table is
  // smaller than the index list it replaces even for the widest legal set.
  // A value index past the end is not in the set, which is the answer an entry
  // would have carried anyway.
  std::vector<bool> members_;
};

/// @brief LIKE pattern matching: param LIKE pattern.
///
/// Supports `*` (any string) and `?` (single character) wildcards.
/// The pattern is matched against the string value of the parameter.
/// Matching honors @p case_sensitive so it is consistent with the other
/// value-matching operators (case-insensitive by default).
/// Matching results are precomputed at construction time for efficiency.
class LikeNode : public ConstraintNode {
 public:
  /// @param param_index Index of the parameter.
  /// @param pattern The glob-like pattern string.
  /// @param param_values The string values of the parameter (used to precompute matches).
  /// @param case_sensitive When false (default), pattern and values match case-insensitively.
  LikeNode(uint32_t param_index, const std::string& pattern,
           const std::vector<std::string>& param_values, bool case_sensitive = false);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

  /// @brief Test whether pre-decomposed text matches a pre-decomposed pattern.
  ///
  /// This overload carries the matching loop. Callers that match one pattern
  /// against many values decompose the pattern once and reuse it.
  /// @param pattern_codepoints The pattern, as Unicode codepoints.
  /// @param text_codepoints The text to test, as Unicode codepoints.
  /// @return true if the text matches the pattern.
  static bool GlobMatch(const std::vector<uint32_t>& pattern_codepoints,
                        const std::vector<uint32_t>& text_codepoints);

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

/// @brief Comparison keys for one parameter's value strings.
///
/// Entry @c i is the key of value index @c i. Two values compare equal exactly
/// when their keys are equal. Both parameters of a comparison are interned
/// together, so keys are only meaningful against the partner they were built
/// with.
using ValueKeys = std::vector<uint32_t>;

/// @brief Parameter-to-parameter equality comparison.
///
/// Compares the string values of two parameters. Equal if the string
/// representations match. Matching honors @p case_sensitive so it is consistent
/// with value-to-literal comparisons (case-insensitive by default).
class ParamEqualsNode : public ConstraintNode {
 public:
  /// @param left_param Index of the left parameter.
  /// @param right_param Index of the right parameter.
  /// @param left_values String values of the left parameter.
  /// @param right_values String values of the right parameter.
  /// @param case_sensitive When false (default), values match case-insensitively.
  ParamEqualsNode(uint32_t left_param, uint32_t right_param,
                  const std::vector<std::string>& left_values,
                  const std::vector<std::string>& right_values, bool case_sensitive = false);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  uint32_t left_param_;
  uint32_t right_param_;
  // Value strings are interned at construction, the same way RelationalNode
  // precomputes its numeric conversions, so Evaluate compares two integers and
  // never touches a value string.
  ValueKeys left_keys_;
  ValueKeys right_keys_;
};

/// @brief Parameter-to-parameter inequality comparison.
///
/// Compares the string values of two parameters. Not equal if the string
/// representations differ. Matching honors @p case_sensitive so it is consistent
/// with value-to-literal comparisons (case-insensitive by default).
class ParamNotEqualsNode : public ConstraintNode {
 public:
  /// @param left_param Index of the left parameter.
  /// @param right_param Index of the right parameter.
  /// @param left_values String values of the left parameter.
  /// @param right_values String values of the right parameter.
  /// @param case_sensitive When false (default), values match case-insensitively.
  ParamNotEqualsNode(uint32_t left_param, uint32_t right_param,
                     const std::vector<std::string>& left_values,
                     const std::vector<std::string>& right_values, bool case_sensitive = false);
  ConstraintResult Evaluate(const std::vector<uint32_t>& assignment) const override;

 private:
  uint32_t left_param_;
  uint32_t right_param_;
  ValueKeys left_keys_;
  ValueKeys right_keys_;
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_CONSTRAINT_AST_H_
