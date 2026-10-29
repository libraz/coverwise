/// @file main.cpp
/// @brief coverwise CLI — combinatorial test design tool.
///
/// Usage:
///   coverwise generate input.json > tests.json
///   coverwise analyze --params params.json --tests tests.json
///   coverwise extend --existing tests.json input.json > additional.json
///
/// Exit codes:
///   0 = OK (coverage 100%)
///   1 = Constraint error
///   2 = Insufficient coverage (coverage < 100% for any reason)
///   3 = Invalid input

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "core/generator.h"
#include "json.h"
#include "model/constraint_ast.h"
#include "model/constraint_parser.h"
#include "model/limits.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/surface_error.h"
#include "model/test_case.h"
#include "model_reader.h"
#include "report_writer.h"
#include "validator/coverage_validator.h"

namespace {

// The documented input limits live in model/limits.h and are the same on every
// surface; the CLI reads them from there instead of restating the numbers.
using coverwise::model::kMaxDocumentBytes;

using coverwise::cli::ExtractTestsArray;
using coverwise::cli::JsonParser;
using coverwise::cli::JsonType;
using coverwise::cli::JsonValue;
using coverwise::cli::JsonWriter;
using coverwise::cli::ParseConstraintExpressions;
using coverwise::cli::ParseModelDocument;
using coverwise::cli::ParseModelParameters;
using coverwise::cli::ParseOptionalUint32;
using coverwise::cli::ParseTests;
using coverwise::cli::ReaderError;
using coverwise::cli::TestRowPolicy;
using coverwise::cli::WriteCoverageReport;
using coverwise::cli::WriteGenerateResult;

// Every failure a caller sees — its exit code and its message alike — is read
// off a SurfaceError, which only a model::Error can produce. No subcommand can
// name an exit code: ExitStatus has no constructor taking one.
using coverwise::model::ExitStatus;
using coverwise::model::SurfaceError;

/// @brief Print a failure in the CLI's stderr envelope and surface its status.
ExitStatus Fail(const coverwise::model::Error& error) {
  SurfaceError surfaced(error);
  std::cerr << "error: " << surfaced.text() << "\n";
  return surfaced;
}

/// @brief Report a failure the core never saw. Routing it through a structured
/// Error keeps its message and its exit code paired the same way a core
/// failure's are.
ExitStatus InvalidInput(std::string message) { return Fail(ReaderError(std::move(message))); }

/// @brief Flush standard output and turn a failed write into a failure status.
///
/// A report is delivered only if its bytes reached the caller, and standard
/// output fails late: the writes that fail are the ones the standard library
/// makes on its own schedule, so a reader that closed the pipe (EPIPE), a
/// filesystem with no room left (ENOSPC) and a destination the caller never
/// opened (EBADF) all surface here, long after the last insertion returned.
/// A command that ends without looking would exit 0 beside a truncated
/// document, and a caller gating on the exit code reads that document as a
/// complete result.
/// Both the C++ stream and the C stream underneath it are asked, because which
/// of the two records the failed write depends on whether the two are still
/// synchronized — a detail of the standard library, not of the report.
ExitStatus FinishOutput(ExitStatus status) {
  std::cout.flush();
  const bool written =
      static_cast<bool>(std::cout) && std::fflush(stdout) == 0 && std::ferror(stdout) == 0;
  if (!written) {
    return InvalidInput("cannot write to standard output");
  }
  return status;
}

/// @brief Print usage text and classify the invocation as invalid input.
///
/// Usage text is its own envelope, without the "error: " prefix, so it is
/// printed here rather than through Fail.
ExitStatus UsageError(const std::string& usage) {
  std::cerr << usage;
  return SurfaceError({coverwise::model::Error::Code::kInvalidInput, usage, ""});
}

/// @brief Derive a subcommand's status from a generator result.
///
/// A structured failure takes precedence (a constraint error is exit 1).
/// Otherwise any coverage below 100% — for any reason, independent of maxTests
/// — is insufficient coverage. That shortfall is reported in the JSON body
/// rather than on stderr, so it is turned into a status without printing.
ExitStatus ResultStatus(const coverwise::model::GenerateResult& result) {
  if (!result.error.ok()) {
    return SurfaceError(result.error);
  }
  if (result.coverage < 1.0) {
    return SurfaceError(
        {coverwise::model::Error::Code::kInsufficientCoverage, "Coverage is below 100%", ""});
  }
  return ExitStatus::Success();
}

// ---------------------------------------------------------------------------
// Input reading utility.
// ---------------------------------------------------------------------------

/// @brief True when @p path selects standard input rather than a named file.
bool IsStdinPath(const std::string& path) { return path == "-"; }

/// @brief Human-readable name of an input source, for diagnostics.
std::string InputName(const std::string& path) {
  return IsStdinPath(path) ? "standard input" : "file '" + path + "'";
}

/// @brief Read @p in to end-of-stream into @p out.
///
/// Stops as soon as the size cap is exceeded so a runaway producer cannot make
/// the CLI allocate without bound. Nothing here seeks or sizes the stream, so
/// pipes, FIFOs and process-substitution paths read like regular files.
/// @return true if the stream was consumed within the size cap.
bool ReadStream(std::istream& in, std::string& out, bool& too_large) {
  char buffer[65536];
  while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
    const size_t count = static_cast<size_t>(in.gcount());
    if (out.size() + count > kMaxDocumentBytes) {
      too_large = true;
      return false;
    }
    out.append(buffer, count);
  }
  return !in.bad();
}

/// @brief Read the named file (or pipe) into @p out.
/// @return true if the path was opened and read within the size cap.
bool ReadFile(const std::string& path, std::string& out, bool& too_large) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  return ReadStream(file, out, too_large);
}

/// @brief Read an input source named on the command line into @p out.
///
/// A path of `-` reads standard input, which every subcommand accepts wherever
/// it takes a file, so callers can pipe JSON instead of writing a temporary
/// file. Standard input is consumable only once, so a second `-` in the same
/// invocation is rejected rather than silently yielding empty content.
///
/// Every failure mode gets its own message in @p error: a source that cannot be
/// opened, one that exceeds the size cap, and one that is genuinely empty are
/// distinguishable instead of conflated.
/// @return true if non-empty content was read; otherwise @p error is set.
bool ReadInput(const std::string& path, std::string& out, std::string& error) {
  bool too_large = false;
  if (IsStdinPath(path)) {
    // A completed ReadStream leaves eofbit set, so the stream itself records
    // whether an earlier argument already drained it — no separate flag needed.
    if (std::cin.eof()) {
      error = "standard input can only be read once; '-' was given more than once";
      return false;
    }
    if (!ReadStream(std::cin, out, too_large)) {
      error = too_large ? "standard input exceeds the maximum of " +
                              std::to_string(kMaxDocumentBytes) + " bytes"
                        : "cannot read standard input";
      return false;
    }
  } else if (!ReadFile(path, out, too_large)) {
    error = too_large ? "file '" + path + "' exceeds the maximum of " +
                            std::to_string(kMaxDocumentBytes) + " bytes"
                      : "cannot open file '" + path + "'";
    return false;
  }
  if (out.empty()) {
    error = InputName(path) + " is empty";
    return false;
  }
  return true;
}

/// @brief Emit a structured result error to stderr, if any. Keeps the failure
/// reason visible on the error stream (not just buried in the JSON warnings),
/// consistent with the analyze subcommand and the WASM surface.
void ReportResultError(const coverwise::model::GenerateResult& result) {
  if (result.error.ok()) return;
  std::cerr << "error: " << SurfaceError(result.error).text() << "\n";
}

// ---------------------------------------------------------------------------
// Command implementations.
// ---------------------------------------------------------------------------

ExitStatus RunGenerate(int argc, char* argv[]) {
  if (argc != 3) {
    return UsageError("Usage: coverwise generate <input.json>\n");
  }

  std::string content;
  std::string read_error;
  if (!ReadInput(argv[2], content, read_error)) {
    return InvalidInput(read_error);
  }

  JsonParser parser(content);
  auto json = parser.Parse();
  if (!parser.error().empty()) {
    return InvalidInput("invalid JSON: " + parser.error());
  }
  if (json.type != JsonType::kObject) {
    return InvalidInput("input must be a JSON object");
  }

  coverwise::model::ChargedTextReader budget;
  coverwise::model::GenerateOptions options;
  if (auto error = ParseModelDocument(json, options, budget); !error.ok()) {
    return Fail(error);
  }

  const uint32_t strength = options.strength;
  // The row text read above and the model's own strings are one input, so the
  // gate judges them against one budget.
  auto accepted = coverwise::model::AcceptOptions(std::move(options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  // Generate.
  auto result = coverwise::core::Generate(accepted->get());

  const auto& effective_params =
      result.parameters.empty() ? accepted->get().parameters : result.parameters;

  // Determine status: constraint error (1) > insufficient coverage (2) > OK.
  ExitStatus status = ResultStatus(result);

  WriteGenerateResult(result, effective_params, strength);
  ReportResultError(result);
  return FinishOutput(status);
}

ExitStatus RunAnalyze(int argc, char* argv[]) {
  // Parse flags.
  std::string params_path;
  std::string tests_path;
  std::string constraints_path;
  uint32_t strength = 2;
  bool strength_from_flag = false;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--params" && i + 1 < argc) {
      params_path = argv[++i];
    } else if (arg == "--tests" && i + 1 < argc) {
      tests_path = argv[++i];
    } else if (arg == "--constraints" && i + 1 < argc) {
      constraints_path = argv[++i];
    } else if (arg == "--strength" && i + 1 < argc) {
      std::string value = argv[++i];
      char* end = nullptr;
      long parsed = std::strtol(value.c_str(), &end, 10);
      if (end == value.c_str() || *end != '\0' || parsed < 1 || parsed > UINT32_MAX) {
        return InvalidInput("--strength must be a positive integer (>= 1)");
      }
      strength = static_cast<uint32_t>(parsed);
      strength_from_flag = true;
    } else {
      return InvalidInput("unknown argument '" + arg + "'");
    }
  }

  if (params_path.empty() || tests_path.empty()) {
    return UsageError(
        "Usage: coverwise analyze --params <params.json> --tests <tests.json>"
        " [--strength <n>] [--constraints <file.json>]\n");
  }

  // Read and parse params. The params file may be either a bare array of
  // parameters or an object with a "parameters" array (and optional
  // "constraints"), matching the shape that generate accepts.
  std::string params_content;
  std::string read_error;
  if (!ReadInput(params_path, params_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser params_parser(params_content);
  auto params_json = params_parser.Parse();
  if (!params_parser.error().empty()) {
    return InvalidInput("invalid params JSON: " + params_parser.error());
  }

  std::string error;
  const JsonValue* params_array = &params_json;
  std::vector<std::string> constraint_expressions;
  if (params_json.type == JsonType::kObject) {
    params_array = &params_json["parameters"];
    if (!ParseConstraintExpressions(params_json["constraints"], constraint_expressions, error)) {
      return InvalidInput(error);
    }
    // The documented pipeline pipes one model through generate and then
    // analyze, so a field of that model which defines the coverage universe has
    // to reach the measurement. An explicit --strength wins, because it is an
    // analysis knob the caller chose for this run.
    if (!strength_from_flag && !ParseOptionalUint32(params_json, "strength", 1, strength, error)) {
      return InvalidInput(error);
    }
    // Sub-models give parts of the model their own strength, which the coverage
    // validator has no way to express: it measures one universe at one strength.
    // Measuring such a model as if the sub-models were absent would report a
    // ratio for a universe the caller never described, so it is refused before
    // any report is written. Analyze one sub-model at a time with --strength.
    if (!params_json["subModels"].IsNull()) {
      return InvalidInput(
          "analyze cannot measure a model with 'subModels'; analyze each group separately with"
          " --strength");
    }
  }

  // Analyze uses the same effective boundary-expanded value space as generate.
  // Its --strength is an analysis parameter rather than a property of the model,
  // so the model goes to the acceptance gate at strength 1 and the gate judges
  // the model alone. The requested strength is judged by ValidateCoverage, which
  // is fail-closed about it on every surface: 0, or more than the parameter
  // count, is invalid input rather than a coverage claim over an empty tuple
  // universe.
  coverwise::model::GenerateOptions model_options;
  model_options.strength = 1;
  if (auto model_error = ParseModelParameters(*params_array, model_options); !model_error.ok()) {
    return Fail(model_error);
  }
  std::vector<coverwise::model::Parameter> params = model_options.parameters;

  // Read constraints from a dedicated file if provided. A constraints file is a
  // JSON object with a "constraints" array (or a bare array of strings).
  if (!constraints_path.empty()) {
    std::string constraints_content;
    if (!ReadInput(constraints_path, constraints_content, read_error)) {
      return InvalidInput(read_error);
    }
    JsonParser constraints_parser(constraints_content);
    auto constraints_json = constraints_parser.Parse();
    if (!constraints_parser.error().empty()) {
      return InvalidInput("invalid constraints JSON: " + constraints_parser.error());
    }
    // An explicit --constraints replaces whatever --params declared, so a
    // document this reader cannot turn into a constraint list would erase the
    // model's constraints instead of failing. `jq '.constraints'` emits bare
    // null for a model that has none, which is exactly how such a document
    // reaches here, and a silently unconstrained run reports a coverage
    // shortfall that the caller has no error output to explain.
    if (constraints_json.type != JsonType::kArray && constraints_json.type != JsonType::kObject) {
      return InvalidInput(
          "constraints file must be a JSON array of expressions or an object with a 'constraints'"
          " array");
    }
    if (constraints_json.type == JsonType::kObject &&
        (!constraints_json.HasKey("constraints") ||
         constraints_json["constraints"].type != JsonType::kArray)) {
      return InvalidInput("constraints file object must have a 'constraints' JSON array");
    }
    const JsonValue& constraints_node = constraints_json.type == JsonType::kObject
                                            ? constraints_json["constraints"]
                                            : constraints_json;
    constraint_expressions.clear();
    if (!ParseConstraintExpressions(constraints_node, constraint_expressions, error)) {
      return InvalidInput(error);
    }
  }

  // Read and parse tests.
  std::string tests_content;
  if (!ReadInput(tests_path, tests_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser tests_parser(tests_content);
  auto tests_json = tests_parser.Parse();
  if (!tests_parser.error().empty()) {
    return InvalidInput("invalid tests JSON: " + tests_parser.error());
  }

  // Rows that no longer match the model are carried through with the
  // mismatching positions unassigned; ValidateCoverage classifies them into
  // invalidTests so the report covers the whole suite instead of stopping at
  // the first drifted row.
  std::vector<coverwise::model::TestCase> tests;
  const JsonValue* tests_array = nullptr;
  coverwise::model::ChargedTextReader budget;
  if (!ExtractTestsArray(tests_json, tests_array, error) ||
      !ParseTests(*tests_array, params, TestRowPolicy::kRecorded, "tests", tests, budget, error)) {
    return InvalidInput(error);
  }

  model_options.constraint_expressions = constraint_expressions;
  auto accepted = coverwise::model::AcceptOptions(std::move(model_options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  // Parse constraint expressions into AST so the validator can exclude
  // constraint-invalid tuples from the coverage universe, consistent with the
  // generator and the WASM/JS surfaces.
  std::vector<coverwise::model::Constraint> constraints;
  for (const auto& expr : constraint_expressions) {
    auto parse_result = coverwise::model::ParseConstraint(expr, params);
    if (!parse_result.error.ok()) {
      return Fail(coverwise::model::AnnotateConstraintError(expr, parse_result.error));
    }
    constraints.push_back(std::move(parse_result.constraint));
  }

  auto report = coverwise::validator::ValidateCoverage(params, tests, strength, constraints);
  if (!report.error.ok()) {
    return Fail(report.error);
  }

  WriteCoverageReport(report);

  // A suite that contains invalid rows (out-of-range, invalid-value, or
  // constraint-violating tests) is malformed input, so exit with invalid-input
  // even when the valid subset covers everything. The details are still in the
  // JSON body (invalidTests). This takes precedence over the coverage shortfall.
  if (!report.invalid_tests.empty()) {
    return FinishOutput(InvalidInput(std::to_string(report.invalid_tests.size()) +
                                     " invalid test(s) in the analyzed suite"));
  }
  if (report.coverage_ratio < 1.0) {
    return FinishOutput(SurfaceError(
        {coverwise::model::Error::Code::kInsufficientCoverage, "Coverage is below 100%", ""}));
  }
  return FinishOutput(ExitStatus::Success());
}

ExitStatus RunExtend(int argc, char* argv[]) {
  // Parse --existing flag and input file.
  std::string existing_path;
  std::string input_path;

  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--existing" && i + 1 < argc) {
      existing_path = argv[++i];
    } else if (input_path.empty() && (IsStdinPath(arg) || (!arg.empty() && arg[0] != '-'))) {
      input_path = arg;
    } else {
      return InvalidInput("unknown argument '" + arg + "'");
    }
  }

  if (existing_path.empty() || input_path.empty()) {
    return UsageError("Usage: coverwise extend --existing <tests.json> <input.json>\n");
  }

  // Read and parse input.
  std::string input_content;
  std::string read_error;
  if (!ReadInput(input_path, input_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser input_parser(input_content);
  auto input_json = input_parser.Parse();
  if (!input_parser.error().empty()) {
    return InvalidInput("invalid input JSON: " + input_parser.error());
  }
  if (input_json.type != JsonType::kObject) {
    return InvalidInput("input must be a JSON object");
  }

  // Extend reads the same model document as generate, so it accepts and rejects
  // exactly what generate does. The document is read up front, which also
  // expands the boundary value space before the --existing rows below are
  // resolved against it, so every row's value indices match the parameters that
  // generation and rendering share.
  std::string error;
  // One budget for the whole invocation: the model document's `seeds` and the
  // `--existing` suite are both caller row text handed to the same run.
  coverwise::model::ChargedTextReader budget;
  coverwise::model::GenerateOptions options;
  if (auto document_error = ParseModelDocument(input_json, options, budget); !document_error.ok()) {
    return Fail(document_error);
  }

  // Read and parse existing tests.
  std::string existing_content;
  if (!ReadInput(existing_path, existing_content, read_error)) {
    return InvalidInput(read_error);
  }
  JsonParser existing_parser(existing_content);
  auto existing_json = existing_parser.Parse();
  if (!existing_parser.error().empty()) {
    return InvalidInput("invalid existing tests JSON: " + existing_parser.error());
  }

  // A recorded suite drifts from the model it was written against: a value gets
  // renamed, a parameter is added. Filling the gap in the model is the whole
  // point of extend, so a drifted row is carried through rather than failing the
  // run. Extend keeps the row as written, the core records why it could not be
  // counted, and the coverage figure is computed without it.
  std::vector<coverwise::model::TestCase> existing;
  const JsonValue* existing_array = nullptr;
  if (!ExtractTestsArray(existing_json, existing_array, error) ||
      !ParseTests(*existing_array, options.parameters, TestRowPolicy::kRecorded, "existing",
                  existing, budget, error)) {
    return InvalidInput(error);
  }

  const uint32_t strength = options.strength;
  auto accepted = coverwise::model::AcceptOptions(std::move(options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  auto result = coverwise::core::Extend(existing, accepted->get(),
                                        coverwise::model::ExtendMode::kStrict, budget.total());

  const auto& effective_params =
      result.parameters.empty() ? accepted->get().parameters : result.parameters;

  // Determine status: constraint error (1) > insufficient coverage (2) > OK.
  ExitStatus status = ResultStatus(result);

  WriteGenerateResult(result, effective_params, strength, existing_array);
  ReportResultError(result);
  return FinishOutput(status);
}

ExitStatus RunStats(int argc, char* argv[]) {
  if (argc != 3) {
    return UsageError("Usage: coverwise stats <input.json>\n");
  }

  std::string content;
  std::string read_error;
  if (!ReadInput(argv[2], content, read_error)) {
    return InvalidInput(read_error);
  }

  JsonParser parser(content);
  auto json = parser.Parse();
  if (!parser.error().empty()) {
    return InvalidInput("invalid JSON: " + parser.error());
  }
  if (json.type != JsonType::kObject) {
    return InvalidInput("input must be a JSON object");
  }

  // Stats is a preflight for generate, so it reads the document through the same
  // reader: a document generate would refuse must not be reported on as if it
  // were a model. None of the figures below is derived from the fields that only
  // generation uses.
  coverwise::model::ChargedTextReader budget;
  coverwise::model::GenerateOptions options;
  if (auto error = ParseModelDocument(json, options, budget); !error.ok()) {
    return Fail(error);
  }

  auto accepted = coverwise::model::AcceptOptions(std::move(options), budget.total());
  if (!accepted.ok()) {
    return Fail(accepted.error());
  }

  auto stats = coverwise::core::EstimateModel(accepted->get());
  if (!stats.error.ok()) {
    return Fail(stats.error);
  }

  JsonWriter w(std::cout);
  w.BeginObject();

  w.Key("schemaVersion");
  w.WriteNumber(1);

  w.Key("parameterCount");
  w.WriteNumber(static_cast<double>(stats.parameter_count));

  w.Key("totalValues");
  w.WriteNumber(static_cast<double>(stats.total_values));

  w.Key("strength");
  w.WriteNumber(static_cast<double>(stats.strength));

  w.Key("totalTuples");
  w.WriteNumber(static_cast<double>(stats.total_tuples));

  w.Key("estimatedTests");
  w.WriteNumber(static_cast<double>(stats.estimated_tests));

  w.Key("subModelCount");
  w.WriteNumber(static_cast<double>(stats.sub_model_count));

  w.Key("constraintCount");
  w.WriteNumber(static_cast<double>(stats.constraint_count));

  w.Key("parameters");
  w.BeginArray();
  for (const auto& pd : stats.parameters) {
    w.Sep();
    w.BeginObject();
    w.Key("name");
    w.WriteString(pd.name);
    w.Key("valueCount");
    w.WriteNumber(static_cast<double>(pd.value_count));
    w.Key("invalidCount");
    w.WriteNumber(static_cast<double>(pd.invalid_count));
    w.EndObject();
  }
  w.EndArray();

  w.EndObject();
  std::cout << '\n';

  return FinishOutput(ExitStatus::Success());
}

const char* UsageText() {
  return "Usage:\n"
         "  coverwise generate <input.json>\n"
         "  coverwise analyze --params <params.json> --tests <tests.json>"
         " [--strength <n>] [--constraints <file.json>]\n"
         "  coverwise extend --existing <tests.json> <input.json>\n"
         "  coverwise stats <input.json>\n"
         "\n"
         "Any input path may be '-' to read that JSON from standard input.\n"
         "\n"
         "Exit codes:\n"
         "  0 = OK (coverage 100%)\n"
         "  1 = Constraint error\n"
         "  2 = Insufficient coverage\n"
         "  3 = Invalid input\n";
}

}  // namespace

int main(int argc, char* argv[]) {
  // `coverwise generate model.json | head` closes the pipe while the report is
  // still being written. The default disposition of SIGPIPE would end the
  // process on the signal, past every exit code this CLI documents and with
  // nothing on stderr, so a caller could not tell a reader that stopped early
  // from a real failure. Ignoring it turns the same event into an EPIPE the
  // write path reports, which FinishOutput reads and converts into exit 3.
  // The macro is POSIX; where it is absent there is no such signal to disarm.
#ifdef SIGPIPE
  std::signal(SIGPIPE, SIG_IGN);
#endif

  try {
    if (argc < 2) {
      return UsageError(UsageText()).exit_code();
    }

    std::string command = argv[1];

    // Usage asked for is information the caller requested and the command
    // produced, so it goes to standard output, where `--help > help.txt` and
    // `--help | grep` can read it. Usage printed because an invocation was
    // wrong is a diagnostic and stays on standard error.
    if (command == "--help" || command == "-h") {
      std::cout << UsageText();
      return FinishOutput(ExitStatus::Success()).exit_code();
    }

    if (command == "generate") {
      return RunGenerate(argc, argv).exit_code();
    }

    if (command == "analyze") {
      return RunAnalyze(argc, argv).exit_code();
    }

    if (command == "extend") {
      return RunExtend(argc, argv).exit_code();
    }

    if (command == "stats") {
      return RunStats(argc, argv).exit_code();
    }

    std::cerr << "Unknown command: " << command << "\n";
    return UsageError(UsageText()).exit_code();
  } catch (const std::exception& error) {
    return InvalidInput(error.what()).exit_code();
  } catch (...) {
    return InvalidInput("unexpected failure").exit_code();
  }
}
