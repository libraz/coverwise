/// @file model_reader.h
/// @brief Reads coverwise model objects from parsed JSON.

#ifndef COVERWISE_TOOLS_MODEL_READER_H_
#define COVERWISE_TOOLS_MODEL_READER_H_

#include <cstdint>
#include <string>
#include <vector>

#include "json.h"
#include "model/error.h"
#include "model/generate_options.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace cli {

coverwise::model::Error ReaderError(std::string message);

bool ParseOptionalUint32(const JsonValue& object, const std::string& field, uint32_t minimum,
                         uint32_t& output, std::string& error);

coverwise::model::Error ParseModelParameters(const JsonValue& parameters_json,
                                             coverwise::model::GenerateOptions& options);

/// @brief How strictly a row array is held to the declared parameter set.
enum class TestRowPolicy {
  /// @brief `seeds`: the row's key set must equal the declared parameter names
  /// and every value must resolve, because a seed is asserted to be a real test
  /// case that generation will build on.
  kSeed,
  /// @brief `tests` / `existing`: a row that no longer matches the model is
  /// carried through with the mismatching positions left unassigned. The
  /// coverage validator classifies it and reports it as an invalid row, so a
  /// single drifted row costs its own coverage rather than the whole run.
  kRecorded,
};

bool ParseTests(const JsonValue& json, const std::vector<coverwise::model::Parameter>& params,
                TestRowPolicy policy, const char* field,
                std::vector<coverwise::model::TestCase>& tests,
                coverwise::model::ChargedTextReader& budget, std::string& error);

bool ExtractTestsArray(const JsonValue& json, const JsonValue*& tests, std::string& error);

bool ParseConstraintExpressions(const JsonValue& json, std::vector<std::string>& expressions,
                                std::string& error);

coverwise::model::Error ParseModelDocument(const JsonValue& json,
                                           coverwise::model::GenerateOptions& options,
                                           coverwise::model::ChargedTextReader& budget);

}  // namespace cli
}  // namespace coverwise

#endif  // COVERWISE_TOOLS_MODEL_READER_H_
