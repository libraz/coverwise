#include "model/parameter.h"

#include <algorithm>
#include <cctype>

namespace coverwise {
namespace model {

uint32_t Parameter::valid_count() const {
  if (invalid_.empty()) return size();
  uint32_t count = 0;
  for (uint32_t i = 0; i < size(); ++i) {
    if (!is_invalid(i)) ++count;
  }
  return count;
}

uint32_t Parameter::invalid_count() const { return size() - valid_count(); }

bool Parameter::is_invalid(uint32_t index) const {
  if (invalid_.empty()) return false;
  if (index >= static_cast<uint32_t>(invalid_.size())) return false;
  return invalid_[index];
}

bool Parameter::has_invalid_values() const {
  for (size_t i = 0; i < invalid_.size(); ++i) {
    if (invalid_[i]) return true;
  }
  return false;
}

const std::vector<std::string>& Parameter::aliases(uint32_t index) const {
  static const std::vector<std::string> kEmpty;
  if (aliases_.empty() || index >= static_cast<uint32_t>(aliases_.size())) {
    return kEmpty;
  }
  return aliases_[index];
}

bool Parameter::has_aliases() const {
  for (const auto& a : aliases_) {
    if (!a.empty()) return true;
  }
  return false;
}

const std::string& Parameter::display_name(uint32_t value_index, uint32_t rotation) const {
  const auto& alias_list = aliases(value_index);
  if (alias_list.empty()) {
    return values[value_index];
  }
  // Total names = 1 (primary) + alias_list.size()
  uint32_t total = 1 + static_cast<uint32_t>(alias_list.size());
  uint32_t pick = rotation % total;
  if (pick == 0) {
    return values[value_index];
  }
  return alias_list[pick - 1];
}

namespace {

/// @brief Compare two strings, optionally case-insensitive.
bool StringsEqual(const std::string& a, const std::string& b, bool case_sensitive) {
  if (case_sensitive) {
    return a == b;
  }
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

}  // namespace

uint32_t Parameter::find_value_index(const std::string& name, bool case_sensitive) const {
  // Check primary values first.
  for (uint32_t i = 0; i < static_cast<uint32_t>(values.size()); ++i) {
    if (StringsEqual(values[i], name, case_sensitive)) return i;
  }
  // Check aliases.
  for (uint32_t i = 0; i < static_cast<uint32_t>(aliases_.size()); ++i) {
    for (const auto& alias : aliases_[i]) {
      if (StringsEqual(alias, name, case_sensitive)) return i;
    }
  }
  return UINT32_MAX;
}

const std::string& Parameter::equivalence_class(uint32_t index) const {
  static const std::string kEmpty;
  if (equivalence_classes_.empty() || index >= static_cast<uint32_t>(equivalence_classes_.size())) {
    return kEmpty;
  }
  return equivalence_classes_[index];
}

bool Parameter::has_equivalence_classes() const {
  for (const auto& c : equivalence_classes_) {
    if (!c.empty()) return true;
  }
  return false;
}

std::vector<std::string> Parameter::unique_classes() const {
  std::vector<std::string> result;
  for (const auto& c : equivalence_classes_) {
    if (!c.empty() && std::find(result.begin(), result.end(), c) == result.end()) {
      result.push_back(c);
    }
  }
  return result;
}

}  // namespace model
}  // namespace coverwise
