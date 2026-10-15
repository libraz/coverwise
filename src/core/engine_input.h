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

/// @brief Validate that a seed can participate in positive coverage.
std::string ValidatePositiveSeed(const model::TestCase& seed,
                                 const std::vector<model::Parameter>& params,
                                 const std::vector<model::Constraint>& constraints,
                                 const SeedValidationSnapshot* original = nullptr);

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
  const model::GenerateOptions& options() const { return options_; }

  /// @brief How many leading `seeds` rows came from an extend call's `existing`.
  ///
  /// Those rows are reported back even when they no longer fit the model; a row
  /// past this prefix is an ordinary seed and is dropped with a warning.
  size_t preserved_seed_count() const { return preserved_seed_count_; }

  /// @brief Shape/index errors against the caller's declared value space.
  ///
  /// Populated only when boundary expansion ran. Expansion can make an
  /// originally out-of-range index name a generated value, so those errors
  /// survive acceptance with their parameter position for ordered validation.
  const std::vector<SeedValidationSnapshot>& original_seed_validations() const {
    return original_seed_validations_;
  }

 private:
  friend AcceptedEngineInput AcceptEngineInput(model::GenerateOptions options,
                                               size_t preserved_seed_count);

  EngineInput(model::GenerateOptions options, size_t preserved_seed_count,
              std::vector<SeedValidationSnapshot> original_seed_validations)
      : options_(std::move(options)),
        preserved_seed_count_(preserved_seed_count),
        original_seed_validations_(std::move(original_seed_validations)) {}

  model::GenerateOptions options_;
  size_t preserved_seed_count_ = 0;
  std::vector<SeedValidationSnapshot> original_seed_validations_;
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
                                               size_t preserved_seed_count);

  explicit AcceptedEngineInput(model::Error error) : error_(std::move(error)) {}
  explicit AcceptedEngineInput(EngineInput value) : value_(std::move(value)) {}

  model::Error error_;
  std::optional<EngineInput> value_;
};

/// @brief Submit options to model::AcceptOptions and mint the engine's token.
///
/// Every public entry point of the engine goes through here, so all of them
/// return exactly the codes the model gate returns for the same options. Seed
/// rows are addressed by index into the value list the caller declared, so the
/// rows are remapped by value identity onto the expanded value space the gate
/// accepted.
///
/// @param preserved_seed_count Leading `seeds` rows to keep verbatim.
AcceptedEngineInput AcceptEngineInput(model::GenerateOptions options, size_t preserved_seed_count);

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_ENGINE_INPUT_H_
