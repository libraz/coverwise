/// @file report_writer.h
/// @brief Writes coverwise results as JSON on standard output.

#ifndef COVERWISE_TOOLS_REPORT_WRITER_H_
#define COVERWISE_TOOLS_REPORT_WRITER_H_

#include <cstdint>
#include <vector>

#include "json.h"
#include "model/parameter.h"
#include "model/test_case.h"
#include "validator/coverage_validator.h"

namespace coverwise {
namespace cli {

void WriteGenerateResult(const coverwise::model::GenerateResult& result,
                         const std::vector<coverwise::model::Parameter>& params, uint32_t strength,
                         const JsonValue* preserved_rows = nullptr);

void WriteCoverageReport(const coverwise::validator::CoverageReport& report);

}  // namespace cli
}  // namespace coverwise

#endif  // COVERWISE_TOOLS_REPORT_WRITER_H_
