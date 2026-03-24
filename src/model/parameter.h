/// @file parameter.h
/// @brief Parameter definition for combinatorial test generation.

#ifndef COVERWISE_MODEL_PARAMETER_H_
#define COVERWISE_MODEL_PARAMETER_H_

#include <cstdint>
#include <string>
#include <vector>

namespace coverwise {
namespace model {

/// @brief A test parameter with a name and a set of discrete values.
///
/// Values can be marked as invalid for negative testing. Invalid values are
/// excluded from positive test generation and used to create separate negative
/// test cases (one invalid value per test case).
struct Parameter {
  std::string name;
  std::vector<std::string> values;

  Parameter() = default;

  /// @brief Construct with name and values (all valid).
  Parameter(std::string name, std::vector<std::string> values)
      : name(std::move(name)), values(std::move(values)) {}

  /// @brief Construct with name, values, and per-value invalid flags.
  Parameter(std::string name, std::vector<std::string> values, std::vector<bool> invalid)
      : name(std::move(name)), values(std::move(values)), invalid_(std::move(invalid)) {}

  /// @brief Returns the number of values for this parameter.
  uint32_t size() const { return static_cast<uint32_t>(values.size()); }

  /// @brief Returns the number of valid values.
  uint32_t valid_count() const;

  /// @brief Returns the number of invalid values.
  uint32_t invalid_count() const;

  /// @brief Returns true if the value at the given index is marked invalid.
  bool is_invalid(uint32_t index) const;

  /// @brief Returns true if this parameter has any invalid values.
  bool has_invalid_values() const;

  /// @brief Access the invalid flags vector.
  const std::vector<bool>& invalid() const { return invalid_; }

  /// @brief Set the invalid flags vector.
  void set_invalid(std::vector<bool> inv) { invalid_ = std::move(inv); }

  /// @brief Returns the aliases for the value at the given index.
  /// Returns an empty vector if no aliases are defined.
  const std::vector<std::string>& aliases(uint32_t index) const;

  /// @brief Returns true if any value has aliases.
  bool has_aliases() const;

  /// @brief Get a display name for a value, rotating through primary + aliases.
  ///
  /// For a value with aliases ["chrome", "edge"], rotation 0 returns the primary
  /// value, rotation 1 returns "chrome", rotation 2 returns "edge", then wraps.
  /// @param value_index Index of the value.
  /// @param rotation Counter used to select which name to display.
  const std::string& display_name(uint32_t value_index, uint32_t rotation) const;

  /// @brief Set the aliases for all values.
  /// aliases[i] is the list of aliases for values[i]. Empty inner vector = no aliases.
  void set_aliases(std::vector<std::vector<std::string>> aliases) { aliases_ = std::move(aliases); }

  /// @brief Access the aliases vector.
  const std::vector<std::vector<std::string>>& all_aliases() const { return aliases_; }

  /// @brief Find a value index by name, checking both primary values and aliases.
  /// @return The value index, or UINT32_MAX if not found.
  uint32_t find_value_index(const std::string& name) const;

 private:
  /// @brief Per-value invalid flag. invalid_[i] = true if values[i] is invalid.
  /// Empty means all values are valid.
  std::vector<bool> invalid_;

  /// @brief Per-value alias list. aliases_[i] = aliases for values[i].
  /// Empty means no aliases for any value.
  std::vector<std::vector<std::string>> aliases_;
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_PARAMETER_H_
