/// @file options_validation.h
/// @brief The single acceptance gate for public generation options.

#ifndef COVERWISE_MODEL_OPTIONS_VALIDATION_H_
#define COVERWISE_MODEL_OPTIONS_VALIDATION_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "model/generate_options.h"

namespace coverwise {
namespace model {

class AcceptedOptions;

/// @brief The one sentence reporting that the aggregate string budget is spent.
///
/// The aggregate limit is documented per input rather than per surface, so a
/// surface that charges caller text outside this gate — a reader accounting for
/// test rows before they are resolved to value indices, say — rejects with this
/// text rather than composing its own. Defined once so no surface can agree on
/// the number while drifting on the words.
std::string AggregateBudgetExceededMessage();

/// @brief Validate a GenerateOptions object against the full acceptance rules.
///
/// This is the rule set alone, judging the value space it is handed. Callers go
/// through AcceptOptions instead, which runs boundary expansion first so the
/// rules are applied to the value space generation will actually use.
Error ValidateGenerateOptions(const GenerateOptions& options);

/// @brief Validate every boundary config and expand the parameters it covers.
///
/// On success @p options carries the expanded value space and no boundary
/// configs, so a later expansion is a no-op. Surfaces call this before
/// resolving `seeds` / `existing` rows, which need the final value list to map
/// a value name to an index; AcceptOptions runs it again, and because it is
/// idempotent the split is a convenience rather than a way around the gate.
///
/// @return An Error with code kInvalidInput on the first malformed config, or
///         an ok Error.
Error ExpandBoundaries(GenerateOptions& options);

/// @brief Options that have passed the acceptance gate.
///
/// AcceptOptions is the only thing that can produce one — the constructor is
/// private and the gate is its sole friend. A surface therefore cannot hand the
/// engine options it has not submitted for acceptance: deleting the gate call
/// leaves it with nothing of this type to pass on, which is a compile error
/// rather than a silently skipped check.
class ValidatedOptions {
 public:
  ValidatedOptions(const ValidatedOptions&) = default;
  ValidatedOptions(ValidatedOptions&&) = default;
  ValidatedOptions& operator=(const ValidatedOptions&) = default;
  ValidatedOptions& operator=(ValidatedOptions&&) = default;

  /// @brief The accepted options, with boundary parameters already expanded.
  const GenerateOptions& get() const { return options_; }

 private:
  friend AcceptedOptions AcceptOptions(GenerateOptions options, size_t charged_bytes);

  explicit ValidatedOptions(GenerateOptions options) : options_(std::move(options)) {}

  GenerateOptions options_;
};

/// @brief Expected<ValidatedOptions, Error> for the acceptance gate.
///
/// Exactly one side is meaningful: when ok() the validated options are
/// available through operator*, otherwise error() describes the rejection.
class AcceptedOptions {
 public:
  bool ok() const { return value_.has_value(); }

  /// @brief The rejection reason. Only meaningful when !ok().
  const Error& error() const { return error_; }

  /// @brief The accepted options. Precondition: ok().
  const ValidatedOptions& operator*() const { return *value_; }
  const ValidatedOptions* operator->() const { return &*value_; }

 private:
  friend AcceptedOptions AcceptOptions(GenerateOptions options, size_t charged_bytes);

  explicit AcceptedOptions(Error error) : error_(std::move(error)) {}
  explicit AcceptedOptions(ValidatedOptions value) : value_(std::move(value)) {}

  Error error_;
  std::optional<ValidatedOptions> value_;
};

/// @brief Run the acceptance gate: expand boundaries, then validate everything.
///
/// This is the one place where a public surface's input is judged. Each surface
/// is responsible only for turning its own representation (JSON text, a JS
/// object) into a GenerateOptions; every rule about what that struct may
/// contain lives here, so the surfaces cannot disagree about what is accepted.
AcceptedOptions AcceptOptions(GenerateOptions options);

/// @brief Run the acceptance gate with part of the string budget already spent.
///
/// A surface reads caller text that the options do not carry to the engine: a
/// test row arrives as value names and reaches the engine as value indices. The
/// documented aggregate limit is per input, not per accumulator, so a surface
/// that charges such text as it reads it hands the total here and the whole
/// input is judged against one budget. Charging it here instead would need a
/// second description of which strings count, which is the thing this gate
/// exists to keep singular.
///
/// @param charged_bytes UTF-8 bytes the caller has already counted toward the
///        aggregate limit. Zero behaves exactly like the single-argument form.
AcceptedOptions AcceptOptions(GenerateOptions options, size_t charged_bytes);

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_OPTIONS_VALIDATION_H_
