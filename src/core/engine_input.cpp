/// @file engine_input.cpp

#include "core/engine_input.h"

#include <string>
#include <utility>
#include <vector>

#include "model/options_validation.h"
#include "util/string_util.h"

namespace coverwise {
namespace core {

using model::GenerateOptions;

namespace {

/// @brief Validate a seed's row shape against a declared parameter set.
std::string ValidateSeedShape(const model::TestCase& seed,
                              const std::vector<model::Parameter>& params) {
  if (seed.values.size() != params.size()) {
    return "expected " + std::to_string(params.size()) + " value(s), got " +
           std::to_string(seed.values.size());
  }
  return {};
}

/// @brief Validate one seed index against a declared parameter set.
std::string ValidateSeedIndex(const model::TestCase& seed,
                              const std::vector<model::Parameter>& params, uint32_t pi) {
  const uint32_t vi = seed.values[pi];
  if (vi < params[pi].size()) return {};
  // A row the caller recorded is described back to them in their own terms.
  // The index here is kUnassigned whenever the row drifted from the model,
  // and printing that sentinel tells the caller nothing about which part of
  // what they submitted no longer fits.
  if (pi < seed.unresolved.size() && !seed.unresolved[pi].empty()) {
    return "value '" + seed.unresolved[pi] + "' is not declared by parameter " + params[pi].name;
  }
  if (vi == model::kUnassigned) {
    return "no value recorded for parameter " + params[pi].name;
  }
  // Reached only through the embedding API, which supplies indices itself,
  // so the index is the caller's own input rather than an internal one.
  return "value index " + std::to_string(vi) + " is out of range for parameter " + params[pi].name;
}

/// @brief Validate seed shape and indices against the declared value space.
SeedValidationSnapshot CaptureSeedValidation(const model::TestCase& seed,
                                             const std::vector<model::Parameter>& params) {
  SeedValidationSnapshot validation;
  validation.error = ValidateSeedShape(seed, params);
  if (!validation.error.empty()) return validation;
  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    const std::string index_error = ValidateSeedIndex(seed, params, pi);
    if (!index_error.empty()) {
      validation.parameter_index = pi;
      validation.error = index_error;
      return validation;
    }
  }
  return validation;
}

/// @brief Move seed value indices onto the expanded value space.
///
/// Seed rows address values by index into the value list the caller declared,
/// while boundary expansion sorts and inserts values, so every index that still
/// names a declared value is looked up again by value identity. A position with
/// an invalid index is left alone for validation to describe; valid positions
/// still retain their declared value identity when another position is invalid.
void RemapSeedValueIndices(const std::vector<model::Parameter>& declared,
                           GenerateOptions& options) {
  for (auto& test : options.seeds) {
    for (size_t pi = 0; pi < test.values.size() && pi < declared.size(); ++pi) {
      const uint32_t old_index = test.values[pi];
      if (old_index >= declared[pi].size()) continue;
      const auto& old_value = declared[pi].values[old_index];
      // Byte equality, not ResolveValueName: the name being looked up is the
      // declared value itself, carried across expansion, not text a caller
      // wrote, so the only match that means "this is the same value" is the
      // exact one.
      uint32_t new_index =
          options.parameters[pi].find_value_index(old_value, /*case_sensitive=*/true);
      if (new_index == model::kUnassigned && util::IsNumeric(old_value)) {
        const double numeric = util::ToDouble(old_value);
        for (uint32_t vi = 0; vi < options.parameters[pi].size(); ++vi) {
          const auto& candidate = options.parameters[pi].values[vi];
          if (util::IsNumeric(candidate) && util::ToDouble(candidate) == numeric) {
            new_index = vi;
            break;
          }
        }
      }
      if (new_index != model::kUnassigned) test.values[pi] = new_index;
    }
  }
}

}  // namespace

/// @brief Validate that a seed can participate in positive coverage.
std::string ValidatePositiveSeed(const model::TestCase& seed,
                                 const std::vector<model::Parameter>& params,
                                 const std::vector<model::Constraint>& constraints,
                                 const SeedValidationSnapshot* original) {
  if (original != nullptr && !original->error.empty() && !original->parameter_index) {
    return original->error;
  }
  const std::string shape_error = ValidateSeedShape(seed, params);
  if (!shape_error.empty()) return shape_error;

  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    if (original != nullptr && original->parameter_index == pi) return original->error;
    const std::string index_error = ValidateSeedIndex(seed, params, pi);
    if (!index_error.empty()) return index_error;
    const uint32_t vi = seed.values[pi];
    if (params[pi].is_invalid(vi)) {
      return "value " + params[pi].name + "=" + params[pi].values[vi] + " is marked invalid";
    }
  }

  for (const auto& constraint : constraints) {
    if (constraint->Evaluate(seed.values) != model::ConstraintResult::kTrue) {
      return "violates a constraint";
    }
  }

  return {};
}

AcceptedEngineInput AcceptEngineInput(GenerateOptions options, size_t preserved_seed_count) {
  // The declared value list is only needed to remap seed rows, and only
  // expansion can move a value's index, so it is copied for that case alone.
  const bool has_boundary_expansion = !options.boundary_configs.empty();
  const bool has_seeds = !options.seeds.empty();
  std::vector<model::Parameter> declared;
  std::vector<SeedValidationSnapshot> original_seed_validations;
  if (has_seeds && has_boundary_expansion) {
    declared = options.parameters;
  }
  auto accepted = model::AcceptOptions(std::move(options), model::ChargedText::None());
  if (!accepted.ok()) return AcceptedEngineInput(accepted.error());
  GenerateOptions engine_options = accepted->get();
  if (has_seeds && has_boundary_expansion) {
    original_seed_validations.reserve(engine_options.seeds.size());
    for (const auto& seed : engine_options.seeds) {
      original_seed_validations.push_back(CaptureSeedValidation(seed, declared));
    }
    RemapSeedValueIndices(declared, engine_options);
  }
  return AcceptedEngineInput(EngineInput(std::move(engine_options), preserved_seed_count,
                                         std::move(original_seed_validations)));
}

}  // namespace core
}  // namespace coverwise
