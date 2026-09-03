/// @file options_validation.h
/// @brief The single acceptance gate for public generation options.

#ifndef COVERWISE_MODEL_OPTIONS_VALIDATION_H_
#define COVERWISE_MODEL_OPTIONS_VALIDATION_H_

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "model/generate_options.h"

namespace coverwise {
namespace model {

class AcceptedOptions;

/// @brief The kinds of caller string the documented byte budgets charge.
///
/// This is the charged set. A string of one of these kinds counts toward both
/// the per-string and the aggregate limit; anything else a caller writes — a
/// field the schema does not read, a row's keys, a number the engine renders
/// itself — counts nowhere. Every surface derives its walk from this list
/// instead of deciding for itself what to count, which is what makes one input
/// cost the same whichever surface reads it. `CHARGED_STRING_KINDS` in
/// src/ts/model/budget.ts is the TypeScript half of the same list.
enum class ChargedString {
  kParameterName,
  kParameterValue,
  kValueAlias,
  kEquivalenceClass,
  kConstraintExpression,
  kSubModelParameterName,
  kWeightParameterName,
  kWeightValueName,
  /// @brief A string a caller wrote in a `tests` / `seeds` / `existing` row.
  ///
  /// The gate never sees one: a row reaches it as value indices, so this kind
  /// is charged by whichever surface read the row, and by that surface alone.
  kRowValue,
};

/// @brief Where a charged string sits, for the kinds whose context names it.
struct ChargedStringLocation {
  /// @brief The parameter the string belongs to, or — for kRowValue — the
  /// caller's own name for the row array. Empty for the kinds that name none.
  std::string_view subject;
  /// @brief The value index, or — for kRowValue — the row index. Ignored by the
  /// kinds that name no position.
  size_t index = 0;
};

/// @brief What a refusal calls one charged string.
///
/// The spelling belongs to the kind rather than to the surface that caught it,
/// so a caller comparing what two surfaces said about one string is comparing
/// one sentence. `chargedStringContext` in src/ts/model/budget.ts mirrors it.
std::string ChargedStringContext(ChargedString kind, const ChargedStringLocation& location);

/// @brief The one sentence reporting that a single string is over its limit.
///
/// @param context What the string is, from ChargedStringContext.
std::string StringBudgetExceededMessage(const std::string& context);

/// @brief The one sentence reporting that the aggregate string budget is spent.
///
/// The aggregate limit is documented per input rather than per surface, so a
/// surface that charges caller text outside this gate — a reader accounting for
/// test rows before they are resolved to value indices, say — rejects with this
/// text rather than composing its own. Defined once so no surface can agree on
/// the number while drifting on the words.
std::string AggregateBudgetExceededMessage();

/// @brief Caller text a surface read itself, and whether it read any.
///
/// This is not a byte count with a name on it. Its two spellings are two
/// accounting regimes, and which one a call site names decides what the gate
/// still has to charge:
///
/// - ChargedText::None() says no reader counted anything. Row values reach the
///   gate only as the text of a position that did not resolve, and nothing
///   upstream has seen them, so the gate charges every kind including
///   kRowValue. An embedder that fills `seeds[].unresolved` by hand is held to
///   the published budget like anyone else.
/// - ChargedTextReader::total() says a reader counted the caller's text, row
///   values included. The gate then charges the model's own strings and leaves
///   the rows alone, because charging them again would cost a caller twice for
///   text they wrote once.
///
/// Between them every string is charged exactly once on every path, and a call
/// site cannot land in either regime without saying so: there is no public
/// constructor, so zero cannot arrive by default.
class ChargedText {
 public:
  /// @brief No reader counted anything; the gate charges the whole input.
  static ChargedText None() { return ChargedText(0, false); }

  /// @brief UTF-8 bytes already spent before the gate walks the options.
  size_t bytes() const { return bytes_; }

  /// @brief Whether a reader already counted the caller's row values.
  bool rows_counted() const { return rows_counted_; }

 private:
  friend class ChargedTextReader;

  ChargedText(size_t bytes, bool rows_counted) : bytes_(bytes), rows_counted_(rows_counted) {}

  size_t bytes_;
  bool rows_counted_;
};

/// @brief The running total of one surface's own reading, for one call.
///
/// One per call rather than one per argument: `extend` reads both `existing`
/// and `seeds`, and two half-sized suites are the same dimension of input as
/// one full-sized one. Counting is all it does — whether the total is too large
/// is the gate's judgement, and the sentence reporting it is the gate's too.
///
/// Holding one of these is the claim that this surface reads the caller's row
/// values, which is why its total puts the gate in the other regime even when
/// the suite happened to be empty: what matters is who is accounting, not how
/// much this particular input came to.
class ChargedTextReader {
 public:
  /// @brief Charge @p bytes of caller text against this call's total.
  void Charge(size_t bytes) { bytes_ += bytes; }

  /// @brief Everything read so far, for the gate.
  ChargedText total() const { return ChargedText(bytes_, true); }

 private:
  size_t bytes_ = 0;
};

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
  friend AcceptedOptions AcceptOptions(GenerateOptions options, ChargedText charged);

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
  friend AcceptedOptions AcceptOptions(GenerateOptions options, ChargedText charged);

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
///
/// Which strings the gate charges is decided by @p charged, and the two regimes
/// together charge every string exactly once: a surface that counted the
/// caller's row values hands its total in and the gate charges the model's own
/// strings, while a caller that counted nothing has the gate charge the row
/// text too. Neither the set nor its wording is described anywhere else, which
/// is the thing this gate exists to keep singular.
///
/// @param charged What the surface already counted, and whether it counted the
///        caller's row values at all.
AcceptedOptions AcceptOptions(GenerateOptions options, ChargedText charged);

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_OPTIONS_VALIDATION_H_
