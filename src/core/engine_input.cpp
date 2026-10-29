/// @file engine_input.cpp

#include "core/engine_input.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "model/limits.h"
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
  if (pi < seed.unresolved.size() && seed.unresolved[pi].has_value()) {
    return "value '" + *seed.unresolved[pi] + "' is not declared by parameter " + params[pi].name;
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
/// names a declared value is looked up again by value identity. An index that
/// names no declared value becomes kUnassigned, so it cannot read as a value
/// expansion inserted; validation describes it from the declared snapshot.
void RemapSeedValueIndices(const std::vector<model::Parameter>& declared,
                           const std::vector<model::Parameter>& expanded,
                           std::vector<SeedRow>& rows) {
  for (auto& row : rows) {
    auto& values = row.test.values;
    for (size_t pi = 0; pi < values.size() && pi < declared.size(); ++pi) {
      const uint32_t old_index = values[pi];
      if (old_index >= declared[pi].size()) {
        values[pi] = model::kUnassigned;
        continue;
      }
      const auto& old_value = declared[pi].values[old_index];
      // Byte equality, not ResolveValueName: the name being looked up is the
      // declared value itself, carried across expansion, not text a caller
      // wrote, so the only match that means "this is the same value" is the
      // exact one.
      uint32_t new_index = expanded[pi].find_value_index(old_value, /*case_sensitive=*/true);
      if (new_index == model::kUnassigned && util::IsNumeric(old_value)) {
        const double numeric = util::ToDouble(old_value);
        for (uint32_t vi = 0; vi < expanded[pi].size(); ++vi) {
          const auto& candidate = expanded[pi].values[vi];
          if (util::IsNumeric(candidate) && util::ToDouble(candidate) == numeric) {
            new_index = vi;
            break;
          }
        }
      }
      values[pi] = new_index;
    }
  }
}

/// @brief Validate that a seed can participate in positive coverage.
std::string ValidatePositiveSeed(const model::TestCase& seed,
                                 const std::vector<model::Parameter>& params,
                                 const std::vector<model::Constraint>& constraints,
                                 const SeedValidationSnapshot& original) {
  if (!original.error.empty() && !original.parameter_index) return original.error;
  const std::string shape_error = ValidateSeedShape(seed, params);
  if (!shape_error.empty()) return shape_error;

  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    if (original.parameter_index == pi) return original.error;
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

/// @brief Whether a row has exactly one invalid value and otherwise fits the model.
bool IsSingleFaultRow(const SeedRow& row, const std::vector<model::Parameter>& params,
                      const std::vector<model::Constraint>& constraints) {
  if (!row.original.error.empty() || row.test.values.size() != params.size()) return false;
  uint32_t faults = 0;
  for (uint32_t pi = 0; pi < static_cast<uint32_t>(params.size()); ++pi) {
    const uint32_t vi = row.test.values[pi];
    if (vi >= params[pi].size()) return false;
    if (params[pi].is_invalid(vi)) ++faults;
  }
  if (faults != 1) return false;
  for (const auto& constraint : constraints) {
    if (constraint->Evaluate(row.test.values) != model::ConstraintResult::kTrue) return false;
  }
  return true;
}

}  // namespace

SeedClassification ClassifySeedRow(const SeedRow& row, const std::vector<model::Parameter>& params,
                                   const std::vector<model::Constraint>& constraints) {
  SeedClassification classification;
  classification.error = ValidatePositiveSeed(row.test, params, constraints, row.original);
  // Only a preserved row is reported back, so only it can stand for a negative case.
  classification.single_fault =
      !classification.error.empty() && row.preserved && IsSingleFaultRow(row, params, constraints);
  return classification;
}

AcceptedEngineInput AcceptEngineInput(GenerateOptions options,
                                      const std::vector<model::TestCase>& existing,
                                      model::ChargedText charged) {
  if (!existing.empty()) {
    if (options.max_tests > 0 && existing.size() > static_cast<size_t>(options.max_tests)) {
      return AcceptedEngineInput(
          model::Error{model::Error::Code::kInvalidInput,
                       "maxTests cannot be smaller than the existing test count",
                       "maxTests=" + std::to_string(options.max_tests) +
                           ", existing=" + std::to_string(existing.size())});
    }
    // Both arrays become one suite, so they share one row limit, and the
    // refusal names the pair rather than a seed count the caller never wrote.
    const size_t row_count = existing.size() + options.seeds.size();
    if (row_count > model::kMaxTests) {
      return AcceptedEngineInput(
          model::Error{model::Error::Code::kInvalidInput,
                       "Existing and seed test count " + std::to_string(row_count) +
                           " exceeds maximum of " + std::to_string(model::kMaxTests),
                       ""});
    }
    // The gate charges the rows it is shown, so `existing` is shown to it as
    // leading seeds and split off again below.
    options.seeds.insert(options.seeds.begin(), existing.begin(), existing.end());
  }

  // The declared value list is only needed to remap seed rows, and only
  // expansion can move a value's index, so it is copied for that case alone.
  const bool remap = !options.boundary_configs.empty() && !options.seeds.empty();
  std::vector<model::Parameter> declared;
  if (remap) declared = options.parameters;
  auto accepted = model::AcceptOptions(std::move(options), charged);
  if (!accepted.ok()) return AcceptedEngineInput(accepted.error());
  GenerateOptions engine_options = accepted->get();

  std::vector<SeedRow> rows;
  rows.reserve(engine_options.seeds.size());
  for (size_t si = 0; si < engine_options.seeds.size(); ++si) {
    SeedRow row;
    row.preserved = si < existing.size();
    row.source_index = row.preserved ? si : si - existing.size();
    if (remap) row.original = CaptureSeedValidation(engine_options.seeds[si], declared);
    row.test = std::move(engine_options.seeds[si]);
    rows.push_back(std::move(row));
  }
  engine_options.seeds.clear();
  if (remap) RemapSeedValueIndices(declared, engine_options.parameters, rows);
  return AcceptedEngineInput(EngineInput(std::move(engine_options), std::move(rows)));
}

}  // namespace core
}  // namespace coverwise
