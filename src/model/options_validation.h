/// @file options_validation.h
/// @brief Semantic validation for public generation options.

#ifndef COVERWISE_MODEL_OPTIONS_VALIDATION_H_
#define COVERWISE_MODEL_OPTIONS_VALIDATION_H_

#include "model/generate_options.h"

namespace coverwise {
namespace model {

/// Validate a GenerateOptions object before boundary expansion or allocation.
Error ValidateGenerateOptions(const GenerateOptions& options);

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_OPTIONS_VALIDATION_H_
