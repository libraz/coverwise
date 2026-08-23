#include "model/parameter.h"

#include <algorithm>
#include <cctype>
#include <unordered_set>

#include "util/string_util.h"

namespace coverwise {
namespace model {
namespace {

std::string FoldAsciiName(const std::string& value) {
  std::string folded = value;
  for (char& c : folded) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return folded;
}

}  // namespace

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
  return util::CaseInsensitiveEqual(a, b);
}

std::string AsciiCaseFold(std::string value) {
  for (char& c : value) {
    unsigned char uc = static_cast<unsigned char>(c);
    if (uc >= 'A' && uc <= 'Z') c = static_cast<char>(uc + ('a' - 'A'));
  }
  return value;
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
  std::unordered_set<std::string> seen;
  for (const auto& c : equivalence_classes_) {
    if (!c.empty() && seen.insert(c).second) {
      result.push_back(c);
    }
  }
  return result;
}

Error ValidateParameters(const std::vector<Parameter>& params) {
  // Checked before the per-parameter rules so an oversized model is rejected up
  // front, in particular before any caller starts a feasibility search over it.
  if (params.size() > kMaxParameters) {
    return {Error::Code::kInvalidInput,
            "Parameter count " + std::to_string(params.size()) + " exceeds maximum of " +
                std::to_string(kMaxParameters),
            ""};
  }
  std::unordered_set<std::string> seen_names;
  std::unordered_set<std::string> seen_folded_names;
  for (const auto& p : params) {
    if (p.name.empty()) {
      return {Error::Code::kInvalidInput, "Parameter name must be a non-empty string", ""};
    }
    if (!seen_names.insert(p.name).second) {
      return {Error::Code::kInvalidInput, "Duplicate parameter name '" + p.name + "'", ""};
    }
    if (!seen_folded_names.insert(FoldAsciiName(p.name)).second) {
      return {Error::Code::kInvalidInput,
              "Parameter names must not differ only by ASCII case: '" + p.name + "'", ""};
    }
    if (p.values.empty()) {
      return {Error::Code::kInvalidInput, "Parameter '" + p.name + "' must have at least one value",
              ""};
    }
    if (p.values.size() > kMaxValuesPerParameter) {
      return {Error::Code::kInvalidInput,
              "Parameter '" + p.name + "' has too many values (maximum " +
                  std::to_string(kMaxValuesPerParameter) + ")",
              ""};
    }
    std::unordered_set<std::string> seen_values;
    for (const auto& v : p.values) {
      if (!seen_values.insert(v).second) {
        return {Error::Code::kInvalidInput,
                "Duplicate value '" + v + "' in parameter '" + p.name + "'", ""};
      }
    }
    if (!p.invalid().empty() && p.invalid().size() != p.values.size()) {
      return {Error::Code::kInvalidInput,
              "Invalid metadata length for parameter '" + p.name + "': invalid", ""};
    }
    if (p.valid_count() == 0) {
      return {Error::Code::kInvalidInput,
              "Parameter '" + p.name + "' must have at least one valid value", ""};
    }
    if (!p.all_aliases().empty() && p.all_aliases().size() != p.values.size()) {
      return {Error::Code::kInvalidInput,
              "Invalid metadata length for parameter '" + p.name + "': aliases", ""};
    }
    if (!p.equivalence_classes().empty() && p.equivalence_classes().size() != p.values.size()) {
      return {Error::Code::kInvalidInput,
              "Invalid metadata length for parameter '" + p.name + "': equivalence classes", ""};
    }

    std::unordered_set<std::string> resolution_names;
    for (const auto& value : p.values) {
      auto canonical = AsciiCaseFold(value);
      if (!resolution_names.insert(canonical).second) {
        return {Error::Code::kInvalidInput,
                "Ambiguous value or alias '" + value + "' in parameter '" + p.name + "'", ""};
      }
    }
    for (const auto& aliases : p.all_aliases()) {
      for (const auto& alias : aliases) {
        auto canonical = AsciiCaseFold(alias);
        if (!resolution_names.insert(canonical).second) {
          return {Error::Code::kInvalidInput,
                  "Ambiguous value or alias '" + alias + "' in parameter '" + p.name + "'", ""};
        }
      }
    }
  }
  return {};
}

}  // namespace model
}  // namespace coverwise
