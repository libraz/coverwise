/// @file engine_input.h
/// @brief The only input the generation engine can be run on.

#ifndef COVERWISE_CORE_ENGINE_INPUT_H_
#define COVERWISE_CORE_ENGINE_INPUT_H_

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "model/error.h"
#include "model/generate_options.h"
#include "model/options_validation.h"

namespace coverwise {
namespace core {

class AcceptedEngineInput;

/// @brief Seed shape/index errors against the caller's declared value space.
///
/// The parameter position is retained so expanded-value validation can keep
/// its original interleaved range/invalid-value diagnostic priority.
struct SeedValidationSnapshot {
  std::string error;
  std::optional<size_t> parameter_index;
};

/// @brief One row the engine is seeded with, in the order it is reported back.
///
/// Extend's `existing` rows come first and are kept verbatim even when they no
/// longer fit the model; the caller's `seeds` follow and are dropped with a
/// warning when they do not.
struct SeedRow {
  /// @brief The row, with its indices moved onto the accepted value space.
  model::TestCase test;
  /// @brief Whether the row came from extend's `existing`.
  bool preserved = false;
  /// @brief Position in the caller's own array, for diagnostics.
  size_t source_index = 0;
  /// @brief Shape/index error against the caller's declared value space.
  ///
  /// Populated only when boundary expansion ran. Expansion can make an
  /// originally out-of-range index name a generated value, so the error
  /// survives acceptance with its parameter position for ordered validation.
  SeedValidationSnapshot original;
};

/// @brief What part a seed row can play in coverage.
struct SeedClassification {
  /// @brief Why the row cannot count toward positive coverage; empty when it can.
  std::string error;
  /// @brief A preserved row with exactly one invalid value that otherwise
  /// satisfies the model, which counts toward negative coverage instead.
  bool single_fault = false;
};

/// @brief Classify a seed row against the accepted parameters and constraints.
SeedClassification ClassifySeedRow(const SeedRow& row, const std::vector<model::Parameter>& params,
                                   const std::vector<model::Constraint>& constraints);

/// @brief Options that reached the engine through the acceptance gate.
///
/// AcceptEngineInput() is the only thing that can produce one — the constructor
/// is private and the acceptance path is its sole friend. The engine
/// implementation takes this type rather than a GenerateOptions, so an entry
/// point that skips acceptance has nothing to pass it: the omission is a
/// compile error instead of a check that silently does not run.
class EngineInput {
 public:
  EngineInput(const EngineInput&) = default;
  EngineInput(EngineInput&&) = default;
  EngineInput& operator=(const EngineInput&) = default;
  EngineInput& operator=(EngineInput&&) = default;

  /// @brief The accepted options, with boundary parameters already expanded.
  ///
  /// `seeds` is empty: the rows have moved to seed_rows().
  const model::GenerateOptions& options() const { return options_; }

  /// @brief Extend's `existing` rows followed by the caller's `seeds`.
  const std::vector<SeedRow>& seed_rows() const { return seed_rows_; }

 private:
  friend AcceptedEngineInput AcceptEngineInput(model::GenerateOptions options,
                                               const std::vector<model::TestCase>& existing,
                                               model::ChargedText charged);

  EngineInput(model::GenerateOptions options, std::vector<SeedRow> seed_rows)
      : options_(std::move(options)), seed_rows_(std::move(seed_rows)) {}

  model::GenerateOptions options_;
  std::vector<SeedRow> seed_rows_;
};

/// @brief Expected<EngineInput, Error> for the engine's acceptance path.
///
/// Exactly one side is meaningful: when ok() the engine input is available
/// through operator*, otherwise error() describes the rejection.
class AcceptedEngineInput {
 public:
  bool ok() const { return value_.has_value(); }

  /// @brief The rejection reason. Only meaningful when !ok().
  const model::Error& error() const { return error_; }

  /// @brief The engine input. Precondition: ok().
  const EngineInput& operator*() const { return *value_; }
  const EngineInput* operator->() const { return &*value_; }

 private:
  friend AcceptedEngineInput AcceptEngineInput(model::GenerateOptions options,
                                               const std::vector<model::TestCase>& existing,
                                               model::ChargedText charged);

  explicit AcceptedEngineInput(model::Error error) : error_(std::move(error)) {}
  explicit AcceptedEngineInput(EngineInput value) : value_(std::move(value)) {}

  model::Error error_;
  std::optional<EngineInput> value_;
};

/// @brief Submit options to model::AcceptOptions and mint the engine's token.
///
/// Every public entry point of the engine goes through here, so all of them
/// return exactly the codes the model gate returns for the same options. This
/// is also the only place extend's `existing` rows enter the engine: they are
/// held to maxTests and to the row limit together with `seeds`, charged by the
/// gate under the same rules as `seeds`, and remapped by value identity onto the
/// expanded value space the gate accepted, as every seed row is.
///
/// @param existing Extend's rows to keep verbatim; empty for every other call.
/// @param charged What the calling surface already counted, handed to the gate
///        unchanged so the rows it read are not charged a second time.
AcceptedEngineInput AcceptEngineInput(model::GenerateOptions options,
                                      const std::vector<model::TestCase>& existing,
                                      model::ChargedText charged);

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_ENGINE_INPUT_H_
