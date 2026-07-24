#include "model/constraint_ast.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace coverwise {
namespace model {

namespace {

std::vector<uint32_t> Utf8Codepoints(const std::string& value) {
  std::vector<uint32_t> result;
  result.reserve(value.size());
  for (size_t i = 0; i < value.size();) {
    const auto first = static_cast<unsigned char>(value[i]);
    uint32_t codepoint = first;
    size_t length = 1;
    if ((first & 0xE0) == 0xC0) {
      codepoint = first & 0x1F;
      length = 2;
    } else if ((first & 0xF0) == 0xE0) {
      codepoint = first & 0x0F;
      length = 3;
    } else if ((first & 0xF8) == 0xF0) {
      codepoint = first & 0x07;
      length = 4;
    }

    bool valid = i + length <= value.size();
    for (size_t offset = 1; valid && offset < length; ++offset) {
      const auto next = static_cast<unsigned char>(value[i + offset]);
      if ((next & 0xC0) != 0x80) {
        valid = false;
      } else {
        codepoint = (codepoint << 6) | (next & 0x3F);
      }
    }
    const bool overlong = (length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
                          (length == 4 && codepoint < 0x10000);
    if (!valid || overlong || (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
        codepoint > 0x10FFFF) {
      // Preserve malformed input deterministically as one unmatched byte.
      result.push_back(0x110000u + first);
      ++i;
    } else {
      result.push_back(codepoint);
      i += length;
    }
  }
  return result;
}

}  // namespace

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

namespace {}  // namespace

NumericValueCachePtr BuildNumericValueCache(const std::vector<std::string>& values) {
  auto cache = std::make_shared<NumericValueCache>();
  cache->numeric.resize(values.size());
  cache->valid.resize(values.size());
  for (size_t i = 0; i < values.size(); ++i) {
    const bool is_num = IsNumeric(values[i]);
    cache->valid[i] = is_num ? 1 : 0;
    cache->numeric[i] = is_num ? ToDouble(values[i]) : 0.0;
  }
  return cache;
}

RelationalNode::RelationalNode(uint32_t param_index, RelOp op, double literal,
                               const std::vector<std::string>& param_values)
    : RelationalNode(param_index, op, literal, BuildNumericValueCache(param_values)) {}

RelationalNode::RelationalNode(uint32_t param_index, RelOp op, double literal,
                               NumericValueCachePtr param_cache)
    : left_param_(param_index),
      op_(op),
      is_param_comparison_(false),
      literal_(literal),
      right_param_(0),
      left_cache_(std::move(param_cache)) {}

RelationalNode::RelationalNode(uint32_t left_param, RelOp op, uint32_t right_param,
                               const std::vector<std::string>& left_values,
                               const std::vector<std::string>& right_values)
    : RelationalNode(left_param, op, right_param, BuildNumericValueCache(left_values),
                     BuildNumericValueCache(right_values)) {}

RelationalNode::RelationalNode(uint32_t left_param, RelOp op, uint32_t right_param,
                               NumericValueCachePtr left_cache, NumericValueCachePtr right_cache)
    : left_param_(left_param),
      op_(op),
      is_param_comparison_(true),
      literal_(0.0),
      right_param_(right_param),
      left_cache_(std::move(left_cache)),
      right_cache_(std::move(right_cache)) {}

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

  if (left_val >= left_cache_->valid.size() || !left_cache_->valid[left_val]) {
    return ConstraintResult::kFalse;
  }
  double left_num = left_cache_->numeric[left_val];

  if (is_param_comparison_) {
    if (right_param_ >= assignment.size()) {
      return ConstraintResult::kUnknown;
    }
    uint32_t right_val = assignment[right_param_];
    if (right_val == kUnassigned) {
      return ConstraintResult::kUnknown;
    }
    if (right_val >= right_cache_->valid.size() || !right_cache_->valid[right_val]) {
      return ConstraintResult::kFalse;
    }
    double right_num = right_cache_->numeric[right_val];
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
  const auto pattern_codepoints = Utf8Codepoints(pattern);
  const auto text_codepoints = Utf8Codepoints(text);
  size_t pi = 0;
  size_t ti = 0;
  size_t star_pi = std::string::npos;
  size_t star_ti = 0;

  while (ti < text_codepoints.size()) {
    if (pi < pattern_codepoints.size() && (pattern_codepoints[pi] == static_cast<uint32_t>('?') ||
                                           pattern_codepoints[pi] == text_codepoints[ti])) {
      ++pi;
      ++ti;
    } else if (pi < pattern_codepoints.size() &&
               pattern_codepoints[pi] == static_cast<uint32_t>('*')) {
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

  while (pi < pattern_codepoints.size() && pattern_codepoints[pi] == static_cast<uint32_t>('*')) {
    ++pi;
  }
  return pi == pattern_codepoints.size();
}

// --- ParamEqualsNode ---

ParamEqualsNode::ParamEqualsNode(uint32_t left_param, uint32_t right_param,
                                 const std::vector<std::string>& left_values,
                                 const std::vector<std::string>& right_values, bool case_sensitive)
    : left_param_(left_param),
      right_param_(right_param),
      case_sensitive_(case_sensitive),
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
  const bool equal = case_sensitive_
                         ? (left_values_[lv] == right_values_[rv])
                         : util::CaseInsensitiveEqual(left_values_[lv], right_values_[rv]);
  return equal ? ConstraintResult::kTrue : ConstraintResult::kFalse;
}

// --- ParamNotEqualsNode ---

ParamNotEqualsNode::ParamNotEqualsNode(uint32_t left_param, uint32_t right_param,
                                       const std::vector<std::string>& left_values,
                                       const std::vector<std::string>& right_values,
                                       bool case_sensitive)
    : left_param_(left_param),
      right_param_(right_param),
      case_sensitive_(case_sensitive),
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
  const bool equal = case_sensitive_
                         ? (left_values_[lv] == right_values_[rv])
                         : util::CaseInsensitiveEqual(left_values_[lv], right_values_[rv]);
  return equal ? ConstraintResult::kFalse : ConstraintResult::kTrue;
}

}  // namespace model
}  // namespace coverwise
