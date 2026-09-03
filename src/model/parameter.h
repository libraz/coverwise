/// @file parameter.h
/// @brief Parameter definition for combinatorial test generation.

#ifndef COVERWISE_MODEL_PARAMETER_H_
#define COVERWISE_MODEL_PARAMETER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "model/error.h"
#include "model/limits.h"

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
  ///
  /// The match policy is deliberately not defaulted. Resolving a name a caller
  /// wrote is ResolveValueName's job and nothing else should be deciding the
  /// policy for itself; a caller that genuinely wants byte equality has to say
  /// so, and one that forgets to decide does not compile.
  ///
  /// @param name The value name to search for.
  /// @param case_sensitive If false, compare names by ASCII case folding.
  /// @return The value index, or UINT32_MAX if not found.
  uint32_t find_value_index(const std::string& name, bool case_sensitive) const;

  /// @brief Returns the equivalence class for the value at the given index.
  /// Returns an empty string if no class is defined.
  const std::string& equivalence_class(uint32_t index) const;

  /// @brief Returns true if any value has an equivalence class defined.
  bool has_equivalence_classes() const;

  /// @brief Returns the distinct class names (in first-seen order).
  std::vector<std::string> unique_classes() const;

  /// @brief Set the equivalence class for each value.
  /// equivalence_classes[i] = class name for values[i]. Empty string = no class.
  void set_equivalence_classes(std::vector<std::string> classes) {
    equivalence_classes_ = std::move(classes);
  }

  /// @brief Access the equivalence classes vector.
  const std::vector<std::string>& equivalence_classes() const { return equivalence_classes_; }

 private:
  /// @brief Per-value invalid flag. invalid_[i] = true if values[i] is invalid.
  /// Empty means all values are valid.
  std::vector<bool> invalid_;

  /// @brief Per-value alias list. aliases_[i] = aliases for values[i].
  /// Empty means no aliases for any value.
  std::vector<std::vector<std::string>> aliases_;

  /// @brief Per-value equivalence class. equivalence_classes_[i] = class for values[i].
  /// Empty means no equivalence classes defined.
  std::vector<std::string> equivalence_classes_;
};

/// @brief Resolve a value name a caller wrote to the index it names.
///
/// This is the one entry point for turning caller-supplied text into a value
/// index: seed rows, `analyze --tests` rows, `extend --existing` rows and
/// weights keys all arrive here, on every surface. The matching policy lives
/// here alone rather than being restated at each call site, so a path added
/// later inherits it instead of choosing again.
///
/// Matching folds ASCII case, which is what the constraint parser does for the
/// same text, so a value spelled one way in a row and another way in a
/// constraint names the same index. The fold is ASCII-only: two names differing
/// only in the case of a non-ASCII letter are distinct names, and one of them
/// does not resolve to the other.
///
/// The answer is unique when it exists because ValidateParameters rejects a
/// parameter whose values or aliases collide once folded.
///
/// @param param The parameter whose values and aliases are searched.
/// @param name The value name as the caller wrote it.
/// @return The value index, or UINT32_MAX if no value or alias names it.
uint32_t ResolveValueName(const Parameter& param, const std::string& name);

/// @brief Check if any parameter in the collection has invalid values.
inline bool HasInvalidValues(const std::vector<Parameter>& params) {
  for (const auto& p : params) {
    if (p.has_invalid_values()) return true;
  }
  return false;
}

/// @brief Validate the semantic well-formedness of a parameter collection.
///
/// Catches input that would otherwise corrupt coverage accounting or silently
/// drop data: an empty parameter name, a parameter with no values, a value that
/// repeats within a single parameter (inflates the tuple denominator and is
/// never coverable past its first occurrence), two parameters sharing a name
/// (their output-map keys collide), or more parameters than kMaxParameters. The
/// messages are kept byte-identical to the TypeScript validator so every
/// surface reports the same text.
///
/// @return An Error with code kInvalidInput on the first violation, or an ok
///         Error when the collection is well-formed.
Error ValidateParameters(const std::vector<Parameter>& params);

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_PARAMETER_H_
