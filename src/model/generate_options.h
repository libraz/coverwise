/// @file generate_options.h
/// @brief Input/output data structures for test generation.

#ifndef COVERWISE_MODEL_GENERATE_OPTIONS_H_
#define COVERWISE_MODEL_GENERATE_OPTIONS_H_

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "model/boundary.h"
#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace model {

/// @brief A sub-model: a subset of parameters with a specific coverage strength.
///
/// Sub-models allow specifying higher coverage strength for critical parameter
/// groups. Parameters not in any sub-model are covered at the global strength.
/// A parameter may appear in multiple sub-models.
struct SubModel {
  std::vector<std::string> parameter_names;  ///< Resolved to indices internally
  uint32_t strength = 2;                     ///< Coverage strength for this group
};

/// @brief Per-value weight configuration for influencing value selection.
///
/// Higher weight = value is preferred when multiple values tie on coverage score.
/// Weight is a hint only; coverage completeness is never compromised.
struct WeightConfig {
  /// @brief weights[param_name][value_name] = weight (default 1.0).
  std::map<std::string, std::map<std::string, double>> entries;

  /// @brief Get the weight for a specific parameter value.
  /// @return The configured weight, or 1.0 if not specified.
  inline double GetWeight(const std::string& param_name, const std::string& value_name) const {
    auto pit = entries.find(param_name);
    if (pit == entries.end()) return 1.0;
    auto vit = pit->second.find(value_name);
    if (vit == pit->second.end()) return 1.0;
    return vit->second;
  }

  /// @brief Check if any weights are configured.
  bool empty() const { return entries.empty(); }
};

/// @brief Options for test generation.
struct GenerateOptions {
  std::vector<Parameter> parameters;
  std::vector<std::string> constraint_expressions;         ///< Constraint strings
  uint32_t strength = 2;                                   ///< Interaction strength (2 = pairwise)
  uint64_t seed = 0;                                       ///< RNG seed for deterministic output
  uint32_t max_tests = 0;                                  ///< 0 = no limit
  std::vector<TestCase> seeds;                             ///< Existing tests to build upon
  std::vector<SubModel> sub_models;                        ///< Mixed-strength sub-models
  WeightConfig weights;                                    ///< Value weight hints
  std::map<std::string, BoundaryConfig> boundary_configs;  ///< Per-param boundary expansion
};

/// @brief Mode for extendTests operation.
enum class ExtendMode {
  kStrict,  ///< Keep existing tests exactly as-is
};

/// @brief Model statistics for preview without running generation.
struct ModelStats {
  uint32_t parameter_count = 0;
  uint32_t total_values = 0;
  uint32_t strength = 0;
  uint32_t total_tuples = 0;
  uint32_t estimated_tests = 0;
  uint32_t sub_model_count = 0;
  uint32_t constraint_count = 0;

  /// @brief Per-parameter detail.
  struct ParamDetail {
    std::string name;
    uint32_t value_count = 0;
    uint32_t invalid_count = 0;
  };
  std::vector<ParamDetail> parameters;
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_GENERATE_OPTIONS_H_
