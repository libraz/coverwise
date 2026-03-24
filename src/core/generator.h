/// @file generator.h
/// @brief Main test generation orchestrator.

#ifndef COVERWISE_CORE_GENERATOR_H_
#define COVERWISE_CORE_GENERATOR_H_

#include <vector>

#include "model/generate_options.h"
#include "model/test_case.h"

namespace coverwise {
namespace core {

/// @brief Generate a covering array for the given options.
/// @return The generated test suite with coverage metadata, stats, and suggestions.
model::GenerateResult Generate(const model::GenerateOptions& options);

/// @brief Extend an existing test suite to improve coverage.
model::GenerateResult Extend(const std::vector<model::TestCase>& existing,
                             const model::GenerateOptions& options,
                             model::ExtendMode mode = model::ExtendMode::kStrict);

/// @brief Estimate model statistics without running generation.
/// @param options The generation options to analyze.
/// @return Model statistics including estimated test count.
model::ModelStats EstimateModel(const model::GenerateOptions& options);

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_GENERATOR_H_
