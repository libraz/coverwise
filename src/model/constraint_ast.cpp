#include "model/constraint_ast.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>

namespace coverwise {
namespace model {

// --- EqualsNode ---

EqualsNode::EqualsNode(uint32_t param_index, uint32_t value_index)
    : param_index_(param_index), value_index_(value_index) {}

ConstraintResult EqualsNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (param_index_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t val = assignment[param_index_];
  if (val == kUnassigned) {
    return ConstraintResult::kUnknown;
  }
  return val == value_index_ ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

// --- NotEqualsNode ---

NotEqualsNode::NotEqualsNode(uint32_t param_index, uint32_t value_index)
    : param_index_(param_index), value_index_(value_index) {}

ConstraintResult NotEqualsNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (param_index_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t val = assignment[param_index_];
  if (val == kUnassigned) {
    return ConstraintResult::kUnknown;
  }
  return val != value_index_ ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

// --- AndNode ---

AndNode::AndNode(Constraint left, Constraint right)
    : left_(std::move(left)), right_(std::move(right)) {}

ConstraintResult AndNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  ConstraintResult l = left_->Evaluate(assignment);
  if (l == ConstraintResult::kFalse) {
    return ConstraintResult::kFalse;
  }
  ConstraintResult r = right_->Evaluate(assignment);
  if (r == ConstraintResult::kFalse) {
    return ConstraintResult::kFalse;
  }
  if (l == ConstraintResult::kTrue && r == ConstraintResult::kTrue) {
    return ConstraintResult::kTrue;
  }
  return ConstraintResult::kUnknown;
}

// --- OrNode ---

OrNode::OrNode(Constraint left, Constraint right)
    : left_(std::move(left)), right_(std::move(right)) {}

ConstraintResult OrNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  ConstraintResult l = left_->Evaluate(assignment);
  if (l == ConstraintResult::kTrue) {
    return ConstraintResult::kTrue;
  }
  ConstraintResult r = right_->Evaluate(assignment);
  if (r == ConstraintResult::kTrue) {
    return ConstraintResult::kTrue;
  }
  if (l == ConstraintResult::kFalse && r == ConstraintResult::kFalse) {
    return ConstraintResult::kFalse;
  }
  return ConstraintResult::kUnknown;
}

// --- NotNode ---

NotNode::NotNode(Constraint child) : child_(std::move(child)) {}

ConstraintResult NotNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  ConstraintResult c = child_->Evaluate(assignment);
  if (c == ConstraintResult::kTrue) {
    return ConstraintResult::kFalse;
  }
  if (c == ConstraintResult::kFalse) {
    return ConstraintResult::kTrue;
  }
  return ConstraintResult::kUnknown;
}

// --- ImpliesNode ---

ImpliesNode::ImpliesNode(Constraint antecedent, Constraint consequent)
    : antecedent_(std::move(antecedent)), consequent_(std::move(consequent)) {}

ConstraintResult ImpliesNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  ConstraintResult ante = antecedent_->Evaluate(assignment);
  if (ante == ConstraintResult::kFalse) {
    return ConstraintResult::kTrue;
  }
  ConstraintResult cons = consequent_->Evaluate(assignment);
  if (ante == ConstraintResult::kTrue) {
    return cons;
  }
  // ante is kUnknown
  if (cons == ConstraintResult::kTrue) {
    return ConstraintResult::kTrue;
  }
  return ConstraintResult::kUnknown;
}

// --- IfThenElseNode ---

IfThenElseNode::IfThenElseNode(Constraint condition, Constraint then_branch, Constraint else_branch)
    : condition_(std::move(condition)),
      then_branch_(std::move(then_branch)),
      else_branch_(std::move(else_branch)) {}

ConstraintResult IfThenElseNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  ConstraintResult cond = condition_->Evaluate(assignment);
  if (cond == ConstraintResult::kTrue) {
    return then_branch_->Evaluate(assignment);
  }
  if (cond == ConstraintResult::kFalse) {
    return else_branch_->Evaluate(assignment);
  }
  // condition is kUnknown: evaluate both branches
  ConstraintResult then_result = then_branch_->Evaluate(assignment);
  ConstraintResult else_result = else_branch_->Evaluate(assignment);
  if (then_result == else_result) {
    return then_result;
  }
  return ConstraintResult::kUnknown;
}

// --- RelationalNode ---

RelationalNode::RelationalNode(uint32_t param_index, RelOp op, double literal,
                               const std::vector<std::string>& param_values)
    : left_param_(param_index),
      op_(op),
      is_param_comparison_(false),
      literal_(literal),
      right_param_(0),
      left_values_(param_values) {}

RelationalNode::RelationalNode(uint32_t left_param, RelOp op, uint32_t right_param,
                               const std::vector<std::string>& left_values,
                               const std::vector<std::string>& right_values)
    : left_param_(left_param),
      op_(op),
      is_param_comparison_(true),
      literal_(0.0),
      right_param_(right_param),
      left_values_(left_values),
      right_values_(right_values) {}

bool RelationalNode::CompareValues(double left, double right) const {
  switch (op_) {
    case RelOp::kLess:
      return left < right;
    case RelOp::kLessEqual:
      return left <= right;
    case RelOp::kGreater:
      return left > right;
    case RelOp::kGreaterEqual:
      return left >= right;
  }
  return false;  // unreachable
}

ConstraintResult RelationalNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (left_param_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t left_val = assignment[left_param_];
  if (left_val == kUnassigned) {
    return ConstraintResult::kUnknown;
  }

  if (left_val >= left_values_.size() || !IsNumeric(left_values_[left_val])) {
    return ConstraintResult::kFalse;
  }
  double left_num = ToDouble(left_values_[left_val]);

  if (is_param_comparison_) {
    if (right_param_ >= assignment.size()) {
      return ConstraintResult::kUnknown;
    }
    uint32_t right_val = assignment[right_param_];
    if (right_val == kUnassigned) {
      return ConstraintResult::kUnknown;
    }
    if (right_val >= right_values_.size() || !IsNumeric(right_values_[right_val])) {
      return ConstraintResult::kFalse;
    }
    double right_num = ToDouble(right_values_[right_val]);
    return CompareValues(left_num, right_num) ? ConstraintResult::kTrue : ConstraintResult::kFalse;
  }

  return CompareValues(left_num, literal_) ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

// --- InNode ---

InNode::InNode(uint32_t param_index, std::vector<uint32_t> value_indices)
    : param_index_(param_index), value_indices_(std::move(value_indices)) {}

ConstraintResult InNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (param_index_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t val = assignment[param_index_];
  if (val == kUnassigned) {
    return ConstraintResult::kUnknown;
  }
  for (uint32_t vi : value_indices_) {
    if (val == vi) {
      return ConstraintResult::kTrue;
    }
  }
  return ConstraintResult::kFalse;
}

// --- LikeNode ---

LikeNode::LikeNode(uint32_t param_index, const std::string& pattern,
                   const std::vector<std::string>& param_values)
    : param_index_(param_index), pattern_(pattern) {
  matches_.resize(param_values.size());
  for (size_t i = 0; i < param_values.size(); ++i) {
    matches_[i] = GlobMatch(pattern_, param_values[i]);
  }
}

ConstraintResult LikeNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (param_index_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t val = assignment[param_index_];
  if (val == kUnassigned) {
    return ConstraintResult::kUnknown;
  }
  if (val >= matches_.size()) {
    return ConstraintResult::kFalse;
  }
  return matches_[val] ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

bool LikeNode::GlobMatch(const std::string& pattern, const std::string& text) {
  size_t pi = 0;
  size_t ti = 0;
  size_t star_pi = std::string::npos;
  size_t star_ti = 0;

  while (ti < text.size()) {
    if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == text[ti])) {
      ++pi;
      ++ti;
    } else if (pi < pattern.size() && pattern[pi] == '*') {
      star_pi = pi;
      star_ti = ti;
      ++pi;
    } else if (star_pi != std::string::npos) {
      pi = star_pi + 1;
      ++star_ti;
      ti = star_ti;
    } else {
      return false;
    }
  }

  while (pi < pattern.size() && pattern[pi] == '*') {
    ++pi;
  }
  return pi == pattern.size();
}

// --- ParamEqualsNode ---

ParamEqualsNode::ParamEqualsNode(uint32_t left_param, uint32_t right_param,
                                 const std::vector<std::string>& left_values,
                                 const std::vector<std::string>& right_values)
    : left_param_(left_param),
      right_param_(right_param),
      left_values_(left_values),
      right_values_(right_values) {}

ConstraintResult ParamEqualsNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (left_param_ >= assignment.size() || right_param_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t lv = assignment[left_param_];
  uint32_t rv = assignment[right_param_];
  if (lv == kUnassigned || rv == kUnassigned) {
    return ConstraintResult::kUnknown;
  }
  if (lv >= left_values_.size() || rv >= right_values_.size()) {
    return ConstraintResult::kFalse;
  }
  return left_values_[lv] == right_values_[rv] ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

// --- ParamNotEqualsNode ---

ParamNotEqualsNode::ParamNotEqualsNode(uint32_t left_param, uint32_t right_param,
                                       const std::vector<std::string>& left_values,
                                       const std::vector<std::string>& right_values)
    : left_param_(left_param),
      right_param_(right_param),
      left_values_(left_values),
      right_values_(right_values) {}

ConstraintResult ParamNotEqualsNode::Evaluate(const std::vector<uint32_t>& assignment) const {
  if (left_param_ >= assignment.size() || right_param_ >= assignment.size()) {
    return ConstraintResult::kUnknown;
  }
  uint32_t lv = assignment[left_param_];
  uint32_t rv = assignment[right_param_];
  if (lv == kUnassigned || rv == kUnassigned) {
    return ConstraintResult::kUnknown;
  }
  if (lv >= left_values_.size() || rv >= right_values_.size()) {
    return ConstraintResult::kFalse;
  }
  return left_values_[lv] != right_values_[rv] ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

}  // namespace model
}  // namespace coverwise
