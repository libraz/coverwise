#include "report_writer.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "model/error.h"
#include "model/surface_error.h"

namespace coverwise {
namespace cli {

using coverwise::model::SurfaceError;

// ---------------------------------------------------------------------------
// Write coverwise output as JSON.
// ---------------------------------------------------------------------------

namespace {

/// @brief Write a parsed scalar JSON value back out unchanged.
void WriteScalar(JsonWriter& w, const JsonValue& value) {
  switch (value.type) {
    case JsonType::kString:
      w.WriteString(value.string_val);
      return;
    case JsonType::kNumber:
      w.WriteNumber(value.number_val);
      return;
    case JsonType::kBool:
      w.WriteBool(value.bool_val);
      return;
    default:
      w.WriteNull();
      return;
  }
}

/// @brief Write the result's tests, echoing a preserved prefix verbatim.
///
/// @param preserved_rows Rows the caller handed to extend, or nullptr. Extend
///        keeps them byte-for-byte, including members the model no longer
///        declares, so they are echoed from the input rather than rendered from
///        value indices — rendering would drop exactly the parts that make a
///        drifted row recognizable.
void WriteResultTests(JsonWriter& w, const coverwise::model::GenerateResult& result,
                      const std::vector<coverwise::model::Parameter>& params,
                      const JsonValue* preserved_rows) {
  const size_t preserved_count = preserved_rows == nullptr ? 0 : preserved_rows->array_val.size();
  for (size_t ti = 0; ti < result.tests.size(); ++ti) {
    w.Sep();
    if (ti < preserved_count) {
      const auto& row = preserved_rows->array_val[ti];
      w.BeginObject();
      for (size_t k = 0; k < row.object_keys.size(); ++k) {
        w.Key(row.object_keys[k]);
        WriteScalar(w, row.object_vals[k]);
      }
      w.EndObject();
      continue;
    }
    const auto& tc = result.tests[ti];
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
      if (tc.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
    }
    w.EndObject();
  }
}

}  // namespace

/// @param preserved_rows The raw `existing` rows extend must echo unchanged, or
///        nullptr for subcommands that render every row from the model.
void WriteGenerateResult(const coverwise::model::GenerateResult& result,
                         const std::vector<coverwise::model::Parameter>& params, uint32_t strength,
                         const JsonValue* preserved_rows) {
  JsonWriter w(std::cout);
  w.BeginObject();
  w.Key("schemaVersion");
  w.WriteNumber(1);

  // tests
  w.Key("tests");
  w.BeginArray();
  WriteResultTests(w, result, params, preserved_rows);
  w.EndArray();
  w.Key("uncoveredCount");
  w.WriteNumber(static_cast<double>(result.uncovered_count));
  w.Key("omittedUncovered");
  w.WriteNumber(static_cast<double>(result.omitted_uncovered));

  // negativeTests is required even when no invalid values are configured.
  w.Key("negativeTests");
  w.BeginArray();
  for (size_t ti = 0; ti < result.negative_tests.size(); ++ti) {
    const auto& tc = result.negative_tests[ti];
    w.Sep();
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < tc.values.size(); ++i) {
      if (tc.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].display_name(tc.values[i], static_cast<uint32_t>(ti)));
    }
    w.EndObject();
  }
  w.EndArray();
  if (result.negative_coverage) {
    w.Key("negativeCoverage");
    w.BeginObject();
    w.Key("totalTuples");
    w.WriteNumber(static_cast<double>(result.negative_coverage->total_tuples));
    w.Key("coveredTuples");
    w.WriteNumber(static_cast<double>(result.negative_coverage->covered_tuples));
    w.Key("omittedTuples");
    w.WriteNumber(static_cast<double>(result.negative_coverage->omitted_tuples));
    w.Key("coverageRatio");
    w.WriteNumber(result.negative_coverage->coverage_ratio);
    w.EndObject();
  }

  // coverage
  w.Key("coverage");
  w.WriteNumber(result.coverage);

  // uncovered
  w.Key("uncovered");
  w.BeginArray();
  for (const auto& u : result.uncovered) {
    w.Sep();
    w.BeginObject();
    w.Key("tuple");
    w.BeginArray();
    for (const auto& s : u.tuple) {
      w.Sep();
      w.WriteString(s);
    }
    w.EndArray();
    w.Key("params");
    w.BeginArray();
    for (const auto& p : u.params) {
      w.Sep();
      w.WriteString(p);
    }
    w.EndArray();
    w.Key("indices");
    w.BeginArray();
    for (const auto& [parameter_index, value_index] : u.indices) {
      w.Sep();
      w.BeginArray();
      w.Sep();
      w.WriteNumber(parameter_index);
      w.Sep();
      w.WriteNumber(value_index);
      w.EndArray();
    }
    w.EndArray();
    w.Key("reason");
    w.WriteString(u.reason);
    w.Key("display");
    w.WriteString(u.ToString());
    w.EndObject();
  }
  w.EndArray();

  // stats
  w.Key("stats");
  w.BeginObject();
  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(result.stats.total_tuples));
  w.Key("coveredTuples");
  w.WriteNumber(static_cast<double>(result.stats.covered_tuples));
  w.Key("testCount");
  w.WriteNumber(static_cast<double>(result.stats.test_count));
  w.EndObject();

  // classCoverage (only when equivalence classes are defined)
  if (result.class_coverage) {
    w.Key("classCoverage");
    w.BeginObject();
    w.Key("totalClassTuples");
    w.WriteNumber(static_cast<double>(result.class_coverage->total_class_tuples));
    w.Key("coveredClassTuples");
    w.WriteNumber(static_cast<double>(result.class_coverage->covered_class_tuples));
    w.Key("classCoverageRatio");
    w.WriteNumber(result.class_coverage->class_coverage_ratio);
    w.EndObject();
  }

  // suggestions
  w.Key("suggestions");
  w.BeginArray();
  for (const auto& s : result.suggestions) {
    w.Sep();
    w.BeginObject();
    w.Key("description");
    w.WriteString(s.description);
    w.Key("testCase");
    w.BeginObject();
    for (size_t i = 0; i < params.size() && i < s.test_case.values.size(); ++i) {
      if (s.test_case.values[i] >= params[i].size()) continue;
      w.Key(params[i].name);
      w.WriteString(params[i].values[s.test_case.values[i]]);
    }
    w.EndObject();
    w.EndObject();
  }
  w.EndArray();

  // warnings
  w.Key("warnings");
  w.BeginArray();
  for (const auto& warn : result.warnings) {
    w.Sep();
    w.WriteString(warn);
  }
  w.EndArray();

  // strength
  w.Key("strength");
  w.WriteNumber(static_cast<double>(strength));

  // error: emitted only on an early-exit failure (constraint parse error,
  // invalid input, tuple explosion), mirroring the WASM envelope's { code,
  // message } so CLI JSON consumers detect failures uniformly across surfaces.
  if (result.error.code != coverwise::model::Error::Code::kOk) {
    w.Key("error");
    w.BeginObject();
    w.Key("code");
    w.WriteNumber(static_cast<double>(static_cast<int>(result.error.code)));
    w.Key("message");
    w.WriteString(SurfaceError(result.error).text());
    w.EndObject();
  }

  w.EndObject();
  std::cout << '\n';
}

void WriteCoverageReport(const coverwise::validator::CoverageReport& report) {
  JsonWriter w(std::cout);
  w.BeginObject();
  w.Key("schemaVersion");
  w.WriteNumber(1);

  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(report.total_tuples));

  w.Key("coveredTuples");
  w.WriteNumber(static_cast<double>(report.covered_tuples));

  w.Key("coverageRatio");
  w.WriteNumber(report.coverage_ratio);

  w.Key("uncovered");
  w.BeginArray();
  for (const auto& u : report.uncovered) {
    w.Sep();
    w.BeginObject();
    w.Key("tuple");
    w.BeginArray();
    for (const auto& s : u.tuple) {
      w.Sep();
      w.WriteString(s);
    }
    w.EndArray();
    w.Key("params");
    w.BeginArray();
    for (const auto& p : u.params) {
      w.Sep();
      w.WriteString(p);
    }
    w.EndArray();
    w.Key("indices");
    w.BeginArray();
    for (const auto& [parameter_index, value_index] : u.indices) {
      w.Sep();
      w.BeginArray();
      w.Sep();
      w.WriteNumber(parameter_index);
      w.Sep();
      w.WriteNumber(value_index);
      w.EndArray();
    }
    w.EndArray();
    w.Key("reason");
    w.WriteString(u.reason);
    w.Key("display");
    w.WriteString(u.ToString());
    w.EndObject();
  }
  w.EndArray();

  w.Key("uncoveredCount");
  w.WriteNumber(static_cast<double>(report.uncovered_count));
  w.Key("omittedUncovered");
  w.WriteNumber(static_cast<double>(report.omitted_uncovered));
  w.Key("invalidTests");
  w.BeginArray();
  for (const auto& invalid : report.invalid_tests) {
    w.Sep();
    w.BeginObject();
    w.Key("testIndex");
    w.WriteNumber(static_cast<double>(invalid.test_index));
    w.Key("reason");
    w.WriteString(invalid.reason);
    w.EndObject();
  }
  w.EndArray();

  w.EndObject();
  std::cout << '\n';
}

}  // namespace cli
}  // namespace coverwise
