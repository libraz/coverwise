#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "model/limits.h"
#include "model/options_validation.h"
#include "model/parameter.h"
#include "model/surface_error.h"
#include "model/test_case.h"

// Path to the built coverwise CLI binary, injected by CMake.
#ifndef COVERWISE_CLI_PATH
#error "COVERWISE_CLI_PATH must be defined by the build"
#endif

namespace {

/// @brief Result of running the CLI: captured stdout and the process exit code.
struct CliResult {
  std::string stdout_text;
  int exit_code = -1;
};

/// @brief Write text to a file, asserting success.
void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out(path);
  ASSERT_TRUE(out.is_open()) << "cannot open " << path;
  out << content;
}

/// @brief Build a unique temp file path for this test process.
std::string TempPath(const std::string& suffix) {
  static int counter = 0;
  std::ostringstream ss;
  ss << "/tmp/coverwise_cli_test_" << getpid() << "_" << counter++ << "_" << suffix;
  return ss.str();
}

/// @brief Run a shell command line, capturing whatever it writes to the pipe.
///
/// The exit code is recovered from popen's pclose status (WEXITSTATUS).
CliResult RunCommandLine(const std::string& command) {
  FILE* pipe = popen(command.c_str(), "r");
  EXPECT_NE(pipe, nullptr) << "popen failed for: " << command;
  CliResult result;
  if (!pipe) return result;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result.stdout_text += buffer;
  }
  int status = pclose(pipe);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

/// @brief Run the CLI with the given argument string, capturing stdout and exit code.
CliResult RunCli(const std::string& args) {
  return RunCommandLine(std::string(COVERWISE_CLI_PATH) + " " + args + " 2>/dev/null");
}

/// @brief Run the CLI capturing merged stdout+stderr and the exit code.
///
/// Unlike RunCli, this keeps stderr so diagnostics can be asserted on.
CliResult RunCliCaptureStderr(const std::string& args) {
  return RunCommandLine(std::string(COVERWISE_CLI_PATH) + " " + args + " 2>&1");
}

/// @brief Run the CLI capturing stderr alone, discarding stdout.
///
/// Lets two subcommands whose success output differs by design be compared on
/// their diagnostics, which are supposed to agree.
CliResult RunCliStderrOnly(const std::string& args) {
  return RunCommandLine(std::string(COVERWISE_CLI_PATH) + " " + args + " 2>&1 1>/dev/null");
}

}  // namespace

// A missing input file and an empty input file produce distinguishable
// diagnostics, both exiting 3 (invalid input).
TEST(CliReadFileTest, MissingVsEmptyFileDistinctDiagnostics) {
  // Missing file: a path that does not exist.
  std::string missing_path = TempPath("does_not_exist.json");
  auto missing = RunCliCaptureStderr("generate " + missing_path);
  EXPECT_EQ(missing.exit_code, 3) << missing.stdout_text;
  EXPECT_NE(missing.stdout_text.find("cannot open file"), std::string::npos) << missing.stdout_text;
  EXPECT_EQ(missing.stdout_text.find("is empty"), std::string::npos) << missing.stdout_text;

  // Empty file: exists but has no content.
  std::string empty_path = TempPath("empty.json");
  WriteFile(empty_path, "");
  auto empty = RunCliCaptureStderr("generate " + empty_path);
  EXPECT_EQ(empty.exit_code, 3) << empty.stdout_text;
  EXPECT_NE(empty.stdout_text.find("is empty"), std::string::npos) << empty.stdout_text;
  EXPECT_EQ(empty.stdout_text.find("cannot open file"), std::string::npos) << empty.stdout_text;
}

TEST(CliInputBudgetTest, RejectsMoreThanTheSupportedParameterCount) {
  std::string input = R"({"parameters":[)";
  for (size_t i = 0; i < 1025; ++i) {
    if (i != 0) input += ',';
    input += R"({"name":"p)" + std::to_string(i) + R"(","values":["v"]})";
  }
  input += "]}";
  const std::string path = TempPath("too_many_parameters.json");
  WriteFile(path, input);
  const auto result = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("parameters exceed maximum"), std::string::npos)
      << result.stdout_text;
}

TEST(CliJsonTest, RejectsTrailingGarbageMalformedNumbersEscapesAndSurrogates) {
  const std::vector<std::string> malformed = {
      R"({"parameters":[{"name":"a","values":["0"]},{"name":"b","values":["0"]}]}) trailing)",
      R"({"parameters":[{"name":"a","values":[0e]},{"name":"b","values":["0"]}]})",
      R"({"parameters":[{"name":"a","values":[01]},{"name":"b","values":["0"]}]})",
      R"({"parameters":[{"name":"a","values":["bad\q"]},{"name":"b","values":["0"]}]})",
      R"({"parameters":[{"name":"a","values":["\uDC00"]},{"name":"b","values":["0"]}]})",
      R"({"parameters":[{"name":"a","values":[1e9999]},{"name":"b","values":["0"]}]})",
  };

  for (size_t i = 0; i < malformed.size(); ++i) {
    const std::string path = TempPath("malformed_" + std::to_string(i) + ".json");
    WriteFile(path, malformed[i]);
    auto result = RunCliCaptureStderr("generate " + path);
    EXPECT_EQ(result.exit_code, 3) << "case " << i << ": " << result.stdout_text;
    EXPECT_NE(result.stdout_text.find("error:"), std::string::npos) << result.stdout_text;
  }
}

TEST(CliJsonTest, RejectsMalformedRawUtf8AndAcceptsValidMultibyteText) {
  const std::string prefix = R"({"parameters":[{"name":"a","values":[")";
  const std::string suffix = R"("]},{"name":"b","values":["0"]}]})";
  const std::vector<std::string> malformed = {
      std::string("\xFF", 1),         std::string("\xC0\xAF", 2),         std::string("\x80", 1),
      std::string("\xED\xA0\x80", 3), std::string("\xF4\x90\x80\x80", 4),
  };
  for (size_t i = 0; i < malformed.size(); ++i) {
    const std::string path = TempPath("invalid_utf8_" + std::to_string(i) + ".json");
    WriteFile(path, prefix + malformed[i] + suffix);
    const auto result = RunCliCaptureStderr("generate " + path);
    EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
    EXPECT_NE(result.stdout_text.find("UTF-8"), std::string::npos) << result.stdout_text;
  }

  const std::string valid_path = TempPath("valid_utf8.json");
  WriteFile(valid_path,
            R"({"parameters":[{"name":"a","values":["日本語"]},{"name":"b","values":["0"]}]})");
  const auto valid = RunCli("generate " + valid_path);
  EXPECT_EQ(valid.exit_code, 0) << valid.stdout_text;
  EXPECT_NE(valid.stdout_text.find("日本語"), std::string::npos);
}

// The depth the reader enforces and the depth its diagnostic names are one
// number. A document nested as deeply as the message says is allowed is read
// without a nesting complaint; only a deeper one is refused for nesting.
TEST(CliJsonTest, TheReportedNestingLimitIsTheOneEnforced) {
  const std::string marker = "JSON nesting depth exceeds ";
  const std::string deep_path = TempPath("deep_nesting.json");
  WriteFile(deep_path, std::string(4096, '[') + std::string(4096, ']'));

  const auto refused = RunCliCaptureStderr("generate " + deep_path);
  EXPECT_EQ(refused.exit_code, 3) << refused.stdout_text;
  const size_t at = refused.stdout_text.find(marker);
  ASSERT_NE(at, std::string::npos) << refused.stdout_text;
  const size_t reported = std::stoul(refused.stdout_text.substr(at + marker.size()));
  ASSERT_GT(reported, 0u);

  // At the reported depth the document is read through: it is refused for what
  // it is — an array where a model object belongs — not for how deeply it nests.
  const std::string at_limit_path = TempPath("nesting_at_limit.json");
  WriteFile(at_limit_path, std::string(reported, '[') + std::string(reported, ']'));
  const auto read = RunCliCaptureStderr("generate " + at_limit_path);
  EXPECT_EQ(read.stdout_text.find(marker), std::string::npos) << read.stdout_text;

  // And past it the nesting is what the document is refused for, so a message
  // naming a bound looser than the one applied is caught from either side.
  const std::string past_limit_path = TempPath("nesting_past_limit.json");
  WriteFile(past_limit_path, std::string(reported + 2, '[') + std::string(reported + 2, ']'));
  const auto past = RunCliCaptureStderr("generate " + past_limit_path);
  EXPECT_NE(past.stdout_text.find(marker), std::string::npos) << past.stdout_text;
}

TEST(CliJsonTest, WriterEscapesEveryC0ControlCharacter) {
  const std::string input = R"({
    "parameters": [
      {"name": "a", "values": ["x\b\f\u0001"]},
      {"name": "b", "values": ["0"]}
    ]
  })";
  const std::string path = TempPath("controls.json");
  WriteFile(path, input);

  auto result = RunCli("generate " + path);

  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("x\\b\\f\\u0001"), std::string::npos) << result.stdout_text;
  for (unsigned char c : result.stdout_text) {
    EXPECT_FALSE(c <= 0x1F && c != '\n' && c != '\r' && c != '\t')
        << "unescaped control byte " << static_cast<int>(c);
  }
}

TEST(CliGenerateTest, RejectsWrongTypeAndOutOfRangeScalars) {
  const std::vector<std::string> fields = {
      R"("strength":"2")",    R"("strength":1.5)", R"("seed":-1)",
      R"("seed":4294967296)", R"("maxTests":-1)",  R"("maxTests":2.5)",
  };
  for (size_t i = 0; i < fields.size(); ++i) {
    const std::string input =
        R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],)" +
        fields[i] + "}";
    const std::string path = TempPath("scalar_" + std::to_string(i) + ".json");
    WriteFile(path, input);
    auto result = RunCliCaptureStderr("generate " + path);
    EXPECT_EQ(result.exit_code, 3) << "case " << i << ": " << result.stdout_text;
  }
}

TEST(CliGenerateTest, RejectsMalformedParameterValueMetadata) {
  const std::vector<std::string> values = {
      R"({"value":"0","invalid":"true"})",
      R"({"value":"0","aliases":"zero"})",
      R"({"value":"0","aliases":[""]})",
      R"({"value":"0","class":7})",
  };
  for (size_t i = 0; i < values.size(); ++i) {
    const std::string input = R"({"parameters":[{"name":"a","values":[)" + values[i] +
                              R"(]},{"name":"b","values":["0"]}]})";
    const std::string path = TempPath("metadata_" + std::to_string(i) + ".json");
    WriteFile(path, input);
    auto result = RunCliCaptureStderr("generate " + path);
    EXPECT_EQ(result.exit_code, 3) << "case " << i << ": " << result.stdout_text;
  }
}

TEST(CliGenerateTest, EmptyBoundaryValuesAreExpanded) {
  const std::string input = R"({
    "parameters": [
      {"name": "n", "values": [], "type": "integer", "range": [0, 1]},
      {"name": "mode", "values": ["on", "off"]}
    ]
  })";
  const std::string path = TempPath("empty_boundary.json");
  WriteFile(path, input);

  auto result = RunCli("generate " + path);
  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;
  for (const char* expected : {"\"n\":\"-1\"", "\"n\":\"0\"", "\"n\":\"1\"", "\"n\":\"2\""}) {
    EXPECT_NE(result.stdout_text.find(expected), std::string::npos) << result.stdout_text;
  }
}

TEST(CliArgumentTest, GenerateAndStatsRejectExtraArguments) {
  const std::string input =
      R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}]})";
  const std::string path = TempPath("extra_args.json");
  WriteFile(path, input);

  EXPECT_EQ(RunCli("generate " + path + " extra").exit_code, 3);
  EXPECT_EQ(RunCli("stats " + path + " extra").exit_code, 3);
}

TEST(CliSchemaTest, GenerateAnalyzeAndStatsUseVersionedStableFields) {
  const std::string input = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "maxTests": 1
  })";
  const std::string input_path = TempPath("schema_input.json");
  WriteFile(input_path, input);

  auto generated = RunCli("generate " + input_path);
  EXPECT_EQ(generated.exit_code, 2) << generated.stdout_text;
  for (const char* field : {"\"schemaVersion\":1", "\"tests\":", "\"negativeTests\":[]",
                            "\"uncoveredCount\":", "\"omittedUncovered\":", "\"stats\":",
                            "\"suggestions\":", "\"warnings\":", "\"strength\":"}) {
    EXPECT_NE(generated.stdout_text.find(field), std::string::npos)
        << "missing " << field << " in " << generated.stdout_text;
  }
  EXPECT_NE(generated.stdout_text.find("\"testCase\":"), std::string::npos)
      << generated.stdout_text;
  EXPECT_NE(generated.stdout_text.find("\"display\":"), std::string::npos) << generated.stdout_text;

  const std::string params_path = TempPath("schema_params.json");
  const std::string tests_path = TempPath("schema_tests.json");
  WriteFile(params_path, R"([{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}])");
  WriteFile(tests_path, R"([{"a":"0","b":"0"}])");
  auto analyzed = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(analyzed.exit_code, 2) << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find("\"schemaVersion\":1"), std::string::npos);
  EXPECT_NE(analyzed.stdout_text.find("\"invalidTests\":[]"), std::string::npos);
  EXPECT_NE(analyzed.stdout_text.find("\"display\":"), std::string::npos);

  auto stats = RunCli("stats " + input_path);
  EXPECT_EQ(stats.exit_code, 0) << stats.stdout_text;
  EXPECT_NE(stats.stdout_text.find("\"schemaVersion\":1"), std::string::npos);
  EXPECT_NE(stats.stdout_text.find("\"subModelCount\":"), std::string::npos);
  EXPECT_NE(stats.stdout_text.find("\"constraintCount\":"), std::string::npos);
  EXPECT_NE(stats.stdout_text.find("\"parameters\":"), std::string::npos);
  EXPECT_EQ(stats.stdout_text.find("\"subModels\":"), std::string::npos);
  EXPECT_EQ(stats.stdout_text.find("\"constraints\":"), std::string::npos);
}

TEST(CliStatsTest, RejectsMalformedConstraintsLikeGenerate) {
  const std::string input = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "constraints": ["unknown = 0"]
  })";
  const std::string path = TempPath("stats_bad_constraint.json");
  WriteFile(path, input);

  const auto generated = RunCliCaptureStderr("generate " + path);
  const auto stats = RunCliCaptureStderr("stats " + path);
  // stats is a preflight for generate, so the same malformed constraint has to
  // be the same failure to a caller: one constraint error, not one constraint
  // error and one invalid input depending on which subcommand asked.
  EXPECT_EQ(generated.exit_code, 1) << generated.stdout_text;
  EXPECT_EQ(stats.exit_code, generated.exit_code) << stats.stdout_text;
  EXPECT_NE(stats.stdout_text.find("Invalid constraint"), std::string::npos) << stats.stdout_text;
}

// stats is a preflight for generate, so the two have to reach the same verdict
// on a model document, with the same diagnostic. `seeds` is part of that
// document: a seed row generate refuses must not pass the preflight, and a
// document generate accepts must not be turned away by it either. The one
// difference stats is entitled to is coverage shortfall (exit 2), which it has
// no opinion on, so every case here is a model generate covers completely.
TEST(CliStatsTest, ReachesTheSameVerdictOnSeedsAsGenerate) {
  struct SeedsCase {
    const char* description;
    const char* document;
    int expected_exit;
    const char* expected_diagnostic;
  };
  const std::vector<SeedsCase> cases = {
      {"a seed row carrying a value the parameter does not declare",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
           "seeds":[{"a":"9","b":"1"}]})",
       3, "seeds 0 parameter 'a' has unknown value '9'"},
      {"a seed row missing a declared parameter",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
           "seeds":[{"a":"0"}]})",
       3, "seeds 0 missing parameter 'b'"},
      {"a seed row carrying a member no parameter declares",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
           "seeds":[{"a":"0","b":"1","c":"1"}]})",
       3, "seeds 0 has unknown parameter 'c'"},
      {"a seed row that is not an object",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
           "seeds":[42]})",
       3, "seeds 0 must be an object"},
      {"a seeds field that is not an array",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
           "seeds":{"a":"0","b":"1"}})",
       3, "seeds must be a JSON array"},
      {"a seeds array both commands accept",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
           "seeds":[{"a":"0","b":"1"}]})",
       0, ""},
      {"a document with no seeds field at all",
       R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}]})", 0,
       ""},
  };

  for (size_t i = 0; i < cases.size(); ++i) {
    const auto& c = cases[i];
    SCOPED_TRACE(c.description);
    const std::string path = TempPath("stats_seeds_" + std::to_string(i) + ".json");
    WriteFile(path, c.document);

    const auto generated = RunCliStderrOnly("generate " + path);
    const auto stats = RunCliStderrOnly("stats " + path);

    EXPECT_EQ(generated.exit_code, c.expected_exit) << generated.stdout_text;
    EXPECT_EQ(stats.exit_code, generated.exit_code) << stats.stdout_text;
    // The message has to match too: agreeing only on "this failed" would let a
    // preflight refuse a document for a reason generation never had.
    EXPECT_EQ(stats.stdout_text, generated.stdout_text);
    EXPECT_NE(generated.stdout_text.find(c.expected_diagnostic), std::string::npos)
        << generated.stdout_text;
  }
}

// Seeds change what generation builds on, but they are not part of any figure
// stats reports, so adding them must not move a single number.
TEST(CliStatsTest, ReportsTheSameFiguresWithAndWithoutSeeds) {
  const std::string without =
      R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}]})";
  const std::string with =
      R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],
          "seeds":[{"a":"0","b":"1"}]})";
  const std::string without_path = TempPath("stats_no_seeds.json");
  const std::string with_path = TempPath("stats_with_seeds.json");
  WriteFile(without_path, without);
  WriteFile(with_path, with);

  const auto plain = RunCli("stats " + without_path);
  const auto seeded = RunCli("stats " + with_path);
  ASSERT_EQ(plain.exit_code, 0) << plain.stdout_text;
  ASSERT_EQ(seeded.exit_code, 0) << seeded.stdout_text;
  EXPECT_EQ(seeded.stdout_text, plain.stdout_text);
}

TEST(CliPipelineTest, GenerateEnvelopeFeedsAnalyzeAndExtendDirectly) {
  const std::string input = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "strength": 2,
    "seed": 42
  })";
  const std::string input_path = TempPath("pipeline_input.json");
  const std::string generated_path = TempPath("pipeline_generated.json");
  WriteFile(input_path, input);

  const auto generated = RunCli("generate " + input_path);
  ASSERT_EQ(generated.exit_code, 0) << generated.stdout_text;
  WriteFile(generated_path, generated.stdout_text);

  const auto analyzed =
      RunCli("analyze --params " + input_path + " --tests " + generated_path + " --strength 2");
  EXPECT_EQ(analyzed.exit_code, 0) << analyzed.stdout_text;

  const auto extended = RunCli("extend --existing " + generated_path + " " + input_path);
  EXPECT_EQ(extended.exit_code, 0) << extended.stdout_text;
  EXPECT_NE(extended.stdout_text.find("\"schemaVersion\":1"), std::string::npos);
}

TEST(CliAnalyzeTest, DedicatedConstraintsObjectRequiresConstraintsArray) {
  const std::string params = R"([{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}])";
  const std::string tests = R"([{"a":"0","b":"0"}])";
  const std::string params_path = TempPath("constraints_params.json");
  const std::string tests_path = TempPath("constraints_tests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  for (const char* invalid : {R"({})", R"({"constraint":["a=0"]})", R"({"constraints":null})"}) {
    const std::string constraints_path = TempPath("constraints_invalid.json");
    WriteFile(constraints_path, invalid);
    const auto result = RunCliCaptureStderr("analyze --params " + params_path + " --tests " +
                                            tests_path + " --constraints " + constraints_path);
    EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  }

  const std::string valid_path = TempPath("constraints_valid.json");
  WriteFile(valid_path, R"({"constraints":["a=0"]})");
  EXPECT_EQ(RunCli("analyze --params " + params_path + " --tests " + tests_path +
                   " --constraints " + valid_path)
                .exit_code,
            2);
}

// analyze with --strength 3 validates a complete 3-wise suite (exit 0).
TEST(CliAnalyzeTest, StrengthThreeCompleteSuiteExitsZero) {
  // Three binary parameters: the 8-row exhaustive truth table is a complete
  // 3-wise covering array.
  const std::string params = R"([
    {"name": "a", "values": ["0", "1"]},
    {"name": "b", "values": ["0", "1"]},
    {"name": "c", "values": ["0", "1"]}
  ])";
  const std::string tests = R"([
    {"a": "0", "b": "0", "c": "0"},
    {"a": "0", "b": "0", "c": "1"},
    {"a": "0", "b": "1", "c": "0"},
    {"a": "0", "b": "1", "c": "1"},
    {"a": "1", "b": "0", "c": "0"},
    {"a": "1", "b": "0", "c": "1"},
    {"a": "1", "b": "1", "c": "0"},
    {"a": "1", "b": "1", "c": "1"}
  ])";
  std::string params_path = TempPath("params.json");
  std::string tests_path = TempPath("tests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  auto result =
      RunCli("analyze --params " + params_path + " --tests " + tests_path + " --strength 3");
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("\"coverageRatio\":1"), std::string::npos)
      << result.stdout_text;
}

// A constraint parse error surfaces on stderr, carries a JSON error envelope,
// and exits 1 (documented constraint-error code) rather than being buried in
// the warnings array.
TEST(CliGenerateTest, ConstraintParseErrorSurfacesErrorEnvelopeAndExitsOne) {
  const std::string input = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "constraints": ["IF a=0 THEN"],
    "strength": 2
  })";
  std::string input_path = TempPath("bad_constraint.json");
  WriteFile(input_path, input);

  auto result = RunCliCaptureStderr("generate " + input_path);
  EXPECT_EQ(result.exit_code, 1) << result.stdout_text;
  // The failure reason reaches stderr / the merged stream.
  EXPECT_NE(result.stdout_text.find("error:"), std::string::npos) << result.stdout_text;
  // The JSON body carries an error envelope mirroring the WASM surface.
  EXPECT_NE(result.stdout_text.find("\"error\""), std::string::npos) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("\"code\":1"), std::string::npos) << result.stdout_text;
}

// Golden output: the CLI generate surface must emit a byte-exact JSON document
// for a fixed model and seed. This anchors the native/CLI surface to an
// absolute value, catching cross-surface drift that WASM-vs-pure parity alone
// cannot (both engines could change identically and still agree). The same
// input+seed is pinned to the same `tests` array in the JS compat suite.
TEST(CliGenerateTest, GoldenOutputIsByteExactForFixedSeed) {
  const std::string input = R"({
    "parameters": [
      {"name": "os", "values": ["win", "mac", "linux"]},
      {"name": "browser", "values": ["chrome", "firefox", "safari"]}
    ],
    "strength": 2,
    "seed": 42
  })";
  const std::string path = TempPath("golden.json");
  WriteFile(path, input);

  auto result = RunCli("generate " + path);
  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;

  const std::string expected =
      R"({"schemaVersion":1,"tests":[{"os":"linux","browser":"firefox"},)"
      R"({"os":"win","browser":"chrome"},{"os":"win","browser":"firefox"},)"
      R"({"os":"mac","browser":"chrome"},{"os":"win","browser":"safari"},)"
      R"({"os":"mac","browser":"firefox"},{"os":"mac","browser":"safari"},)"
      R"({"os":"linux","browser":"safari"},{"os":"linux","browser":"chrome"}],)"
      R"("uncoveredCount":0,"omittedUncovered":0,"negativeTests":[],"coverage":1,)"
      R"("uncovered":[],"stats":{"totalTuples":9,"coveredTuples":9,"testCount":9},)"
      R"("suggestions":[],"warnings":[],"strength":2})"
      "\n";
  EXPECT_EQ(result.stdout_text, expected);
}

// Analyzing a suite that contains a constraint-violating row is invalid input:
// the CLI exits 3 (not 0) even when the valid subset covers everything, and
// lists the offending row in invalidTests.
TEST(CliAnalyzeTest, InvalidTestsExitInvalidInput) {
  const std::string params = R"({
    "parameters": [
      {"name": "os", "values": ["win", "mac"]},
      {"name": "br", "values": ["chrome", "ie"]}
    ],
    "constraints": ["IF os=mac THEN br!=ie"]
  })";
  const std::string tests = R"([
    {"os": "win", "br": "chrome"},
    {"os": "win", "br": "ie"},
    {"os": "mac", "br": "chrome"},
    {"os": "mac", "br": "ie"}
  ])";
  std::string params_path = TempPath("iparams.json");
  std::string tests_path = TempPath("itests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  auto result = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("\"invalidTests\""), std::string::npos) << result.stdout_text;
}

// A complete suite for a constrained model returns coverage 1.0 / exit 0 — the
// headline W-1(a) fix. Without passing constraints, the constraint-invalid tuple
// (os=mac, browser=ie) would be counted as required-uncovered and yield exit 2.
TEST(CliAnalyzeTest, ConstrainedCompleteSuiteExitsZero) {
  const std::string params = R"({
    "parameters": [
      {"name": "os", "values": ["win", "mac"]},
      {"name": "browser", "values": ["chrome", "ie"]}
    ],
    "constraints": ["IF os=mac THEN browser!=ie"]
  })";
  // Complete pairwise suite respecting the constraint: every valid pair is
  // covered, and (mac, ie) is never present.
  const std::string tests = R"([
    {"os": "win", "browser": "chrome"},
    {"os": "win", "browser": "ie"},
    {"os": "mac", "browser": "chrome"}
  ])";
  std::string params_path = TempPath("cparams.json");
  std::string tests_path = TempPath("ctests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  auto result = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("\"coverageRatio\":1"), std::string::npos)
      << result.stdout_text;
}

// Without constraints, the same suite is reported incomplete (exit 2), proving
// the constraint exclusion is what makes the suite complete.
TEST(CliAnalyzeTest, ConstrainedSuiteWithoutConstraintsIsIncomplete) {
  const std::string params = R"([
    {"name": "os", "values": ["win", "mac"]},
    {"name": "browser", "values": ["chrome", "ie"]}
  ])";
  const std::string tests = R"([
    {"os": "win", "browser": "chrome"},
    {"os": "win", "browser": "ie"},
    {"os": "mac", "browser": "chrome"}
  ])";
  std::string params_path = TempPath("uparams.json");
  std::string tests_path = TempPath("utests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  auto result = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 2) << result.stdout_text;
}

// analyze rejects strength 0 with exit 3.
TEST(CliAnalyzeTest, StrengthZeroExitsInvalidInput) {
  const std::string params = R"([{"name": "a", "values": ["0", "1"]}])";
  const std::string tests = R"([{"a": "0"}, {"a": "1"}])";
  std::string params_path = TempPath("zparams.json");
  std::string tests_path = TempPath("ztests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  auto result =
      RunCli("analyze --params " + params_path + " --tests " + tests_path + " --strength 0");
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
}

// analyze rejects an unknown flag with exit 3.
TEST(CliAnalyzeTest, UnknownFlagExitsInvalidInput) {
  const std::string params = R"([{"name": "a", "values": ["0", "1"]}])";
  const std::string tests = R"([{"a": "0"}, {"a": "1"}])";
  std::string params_path = TempPath("kparams.json");
  std::string tests_path = TempPath("ktests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  // A typo of --strength must be rejected, not silently ignored.
  auto result =
      RunCli("analyze --params " + params_path + " --tests " + tests_path + " --stength 2");
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
}

// extend rejects an unknown flag with exit 3.
TEST(CliExtendTest, UnknownFlagExitsInvalidInput) {
  const std::string input = R"({"parameters": [{"name": "a", "values": ["0", "1"]}]})";
  const std::string existing = R"([{"a": "0"}])";
  std::string input_path = TempPath("einput.json");
  std::string existing_path = TempPath("eexisting.json");
  WriteFile(input_path, input);
  WriteFile(existing_path, existing);

  auto result = RunCli("extend --existing " + existing_path + " --bogus " + input_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
}

// generate honors a subModel: a higher-strength sub-model forces full coverage
// of its parameter group, producing more tests than the global pairwise default.
TEST(CliGenerateTest, SubModelRaisesTestCount) {
  // Four binary parameters at global strength 2.
  const std::string base = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]},
      {"name": "c", "values": ["0", "1"]},
      {"name": "d", "values": ["0", "1"]}
    ],
    "seed": 1
  })";
  // Same model, but a, b, c, d covered at strength 4 (exhaustive: 16 rows).
  const std::string with_sub = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]},
      {"name": "c", "values": ["0", "1"]},
      {"name": "d", "values": ["0", "1"]}
    ],
    "seed": 1,
    "subModels": [
      {"parameters": ["a", "b", "c", "d"], "strength": 4}
    ]
  })";
  std::string base_path = TempPath("gbase.json");
  std::string sub_path = TempPath("gsub.json");
  WriteFile(base_path, base);
  WriteFile(sub_path, with_sub);

  auto base_result = RunCli("generate " + base_path);
  auto sub_result = RunCli("generate " + sub_path);
  ASSERT_EQ(base_result.exit_code, 0) << base_result.stdout_text;
  ASSERT_EQ(sub_result.exit_code, 0) << sub_result.stdout_text;

  // The strength-4 sub-model requires all 16 combinations; pairwise alone needs
  // far fewer. Compare reported testCount.
  auto extract_test_count = [](const std::string& json) -> int {
    auto pos = json.find("\"testCount\":");
    EXPECT_NE(pos, std::string::npos) << json;
    if (pos == std::string::npos) return -1;
    return std::atoi(json.c_str() + pos + std::string("\"testCount\":").size());
  };
  int base_count = extract_test_count(base_result.stdout_text);
  int sub_count = extract_test_count(sub_result.stdout_text);
  EXPECT_EQ(sub_count, 16) << sub_result.stdout_text;
  EXPECT_GT(sub_count, base_count) << "sub-model was ignored";
}

// generate honors seed tests: a seed row supplied via the input JSON appears in
// the generated suite verbatim.
TEST(CliGenerateTest, SeedTestAppearsInOutput) {
  const std::string input = R"({
    "parameters": [
      {"name": "os", "values": ["win", "mac", "linux"]},
      {"name": "browser", "values": ["chrome", "safari", "firefox"]}
    ],
    "seeds": [
      {"os": "linux", "browser": "firefox"}
    ]
  })";
  std::string input_path = TempPath("seedinput.json");
  WriteFile(input_path, input);

  auto result = RunCli("generate " + input_path);
  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;
  // The seed row must be present in the emitted tests array.
  EXPECT_NE(result.stdout_text.find("\"os\":\"linux\",\"browser\":\"firefox\""), std::string::npos)
      << result.stdout_text;
}

// generate rejects a parameter with a duplicate value -> exit 3.
TEST(CliGenerateTest, DuplicateValueExitsInvalidInput) {
  const std::string input = R"({
    "parameters": [
      {"name": "os", "values": ["win", "win"]}
    ]
  })";
  std::string input_path = TempPath("dupval.json");
  WriteFile(input_path, input);

  auto result = RunCli("generate " + input_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
}

// generate rejects duplicate parameter names -> exit 3.
TEST(CliGenerateTest, DuplicateParameterNameExitsInvalidInput) {
  const std::string input = R"({
    "parameters": [
      {"name": "os", "values": ["win", "mac"]},
      {"name": "os", "values": ["chrome", "safari"]}
    ]
  })";
  std::string input_path = TempPath("dupname.json");
  WriteFile(input_path, input);

  auto result = RunCli("generate " + input_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
}

// analyze rejects a duplicate-value model -> exit 3.
TEST(CliAnalyzeTest, DuplicateValueExitsInvalidInput) {
  const std::string params = R"([{"name": "os", "values": ["win", "win"]}])";
  const std::string tests = R"([{"os": "win"}])";
  std::string params_path = TempPath("dupanalyze_params.json");
  std::string tests_path = TempPath("dupanalyze_tests.json");
  WriteFile(params_path, params);
  WriteFile(tests_path, tests);

  auto result = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
}

// generate with a numeric boundary parameter renders the expanded boundary
// values as non-empty strings (regression: test cases carry value indices, so
// rendering against unexpanded parameters yielded empty/garbage values).
TEST(CliGenerateTest, BoundaryExpansionRendersNonEmptyValues) {
  // Integer range [0, 5] expands to {-1, 0, 1, 4, 5, 6}; a second parameter
  // makes pairwise coverage non-trivial.
  const std::string input = R"({
    "parameters": [
      {"name": "n", "values": ["0", "5"], "type": "integer", "range": [0, 5]},
      {"name": "mode", "values": ["fast", "slow"]}
    ]
  })";
  std::string input_path = TempPath("boundary.json");
  WriteFile(input_path, input);

  auto result = RunCli("generate " + input_path);
  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;

  // The expanded boundary values must appear in the rendered output.
  for (const char* expected : {"\"n\":\"-1\"", "\"n\":\"0\"", "\"n\":\"1\"", "\"n\":\"4\"",
                               "\"n\":\"5\"", "\"n\":\"6\""}) {
    EXPECT_NE(result.stdout_text.find(expected), std::string::npos)
        << "missing " << expected << " in:\n"
        << result.stdout_text;
  }

  // No rendered value for either parameter may be empty.
  EXPECT_EQ(result.stdout_text.find("\"n\":\"\""), std::string::npos) << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find("\"mode\":\"\""), std::string::npos) << result.stdout_text;

  // Coverage must be complete.
  EXPECT_NE(result.stdout_text.find("\"coverage\":1"), std::string::npos) << result.stdout_text;
}

// Every subcommand accepts '-' in place of an input path and reads that JSON
// from standard input, producing the same output as the file form.
TEST(CliStdinTest, EachCommandReadsInputFromStandardInput) {
  const std::string input = R"({
    "parameters": [
      {"name": "os", "values": ["win", "mac"]},
      {"name": "browser", "values": ["chrome", "safari"]}
    ],
    "seed": 7
  })";
  const std::string input_path = TempPath("stdin_input.json");
  WriteFile(input_path, input);

  const auto generate_file = RunCli("generate " + input_path);
  const auto generate_stdin = RunCli("generate - < " + input_path);
  ASSERT_EQ(generate_stdin.exit_code, 0) << generate_stdin.stdout_text;
  EXPECT_EQ(generate_stdin.stdout_text, generate_file.stdout_text);

  const auto stats_file = RunCli("stats " + input_path);
  const auto stats_stdin = RunCli("stats - < " + input_path);
  ASSERT_EQ(stats_stdin.exit_code, 0) << stats_stdin.stdout_text;
  EXPECT_EQ(stats_stdin.stdout_text, stats_file.stdout_text);

  const std::string tests_path = TempPath("stdin_tests.json");
  WriteFile(tests_path, generate_file.stdout_text);

  // analyze reads either side from standard input; the other stays a file.
  const auto analyze_params_stdin =
      RunCli("analyze --params - --tests " + tests_path + " < " + input_path);
  EXPECT_EQ(analyze_params_stdin.exit_code, 0) << analyze_params_stdin.stdout_text;
  const auto analyze_tests_stdin =
      RunCli("analyze --params " + input_path + " --tests - < " + tests_path);
  EXPECT_EQ(analyze_tests_stdin.exit_code, 0) << analyze_tests_stdin.stdout_text;

  const auto extend_stdin = RunCli("extend --existing " + tests_path + " - < " + input_path);
  EXPECT_EQ(extend_stdin.exit_code, 0) << extend_stdin.stdout_text;
  EXPECT_NE(extend_stdin.stdout_text.find("\"coverage\":1"), std::string::npos)
      << extend_stdin.stdout_text;
}

// Standard input can be consumed only once, so a second '-' is a clear error
// rather than an empty read.
TEST(CliStdinTest, RejectsStandardInputRequestedTwice) {
  const std::string input = R"([{"name": "os", "values": ["win", "mac"]}])";
  const std::string input_path = TempPath("stdin_twice.json");
  WriteFile(input_path, input);

  const auto result = RunCliCaptureStderr("analyze --params - --tests - < " + input_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("standard input can only be read once"), std::string::npos)
      << result.stdout_text;
}

// Empty standard input is reported as empty input, not as an unopenable file.
TEST(CliStdinTest, EmptyStandardInputIsReportedAsEmpty) {
  const auto result = RunCliCaptureStderr("generate - < /dev/null");
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("standard input is empty"), std::string::npos)
      << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find("cannot open file"), std::string::npos) << result.stdout_text;
}

// A single string beyond the documented per-string budget is rejected, from a
// file and from standard input alike — and it is named. A reader that knows
// only that it is inside some string can say the limit but not which string met
// it, which in a document with thousands of them is not an answer.
TEST(CliInputBudgetTest, OversizedSingleStringIsRejectedFromEitherSource) {
  std::string oversized = R"({"parameters":[{"name":"a","values":[")";
  oversized.append(coverwise::model::kMaxStringBytes + 1, 'x');
  oversized += R"("]}]})";
  const std::string path = TempPath("oversized_string.json");
  WriteFile(path, oversized);

  const std::string expected =
      coverwise::model::StringBudgetExceededMessage(coverwise::model::ChargedStringContext(
          coverwise::model::ChargedString::kParameterValue, {"a", 0}));

  const auto from_file = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(from_file.exit_code, 3) << from_file.stdout_text;
  EXPECT_NE(from_file.stdout_text.find(expected), std::string::npos) << from_file.stdout_text;

  const auto from_stdin = RunCliCaptureStderr("generate - < " + path);
  EXPECT_EQ(from_stdin.exit_code, 3) << from_stdin.stdout_text;
  EXPECT_NE(from_stdin.stdout_text.find(expected), std::string::npos) << from_stdin.stdout_text;
}

// A row value over the per-string limit names the array and the row it sits in,
// in the same wording the gate uses for a model string. The reader is a
// different layer, but the limit is the same limit and has one sentence.
TEST(CliInputBudgetTest, OversizedRowValueNamesTheRowItCameFrom) {
  const std::string params_path = TempPath("oversized_row_params.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                         R"({"name":"b","values":["1","2"]}]})");

  std::string suite = R"([{"a":"x","b":"1"},{"a":")";
  suite.append(coverwise::model::kMaxStringBytes + 1, 'x');
  suite += R"(","b":"1"}])";
  const std::string rows_path = TempPath("oversized_row_rows.json");
  WriteFile(rows_path, suite);

  const std::string expected =
      coverwise::model::StringBudgetExceededMessage(coverwise::model::ChargedStringContext(
          coverwise::model::ChargedString::kRowValue, {"tests", 1}));

  const auto result =
      RunCliCaptureStderr("analyze --params " + params_path + " --tests " + rows_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text.substr(0, 200);
  EXPECT_NE(result.stdout_text.find(expected), std::string::npos)
      << result.stdout_text.substr(0, 200);
}

// The budget that bounds a whole document is the aggregate string budget, the
// one the public limits name. The raw-byte guard on a file or stream sits far
// above it, so an input that satisfies the documented per-entity limits is never
// turned away for being long.
TEST(CliInputBudgetTest, AggregateStringBudgetIsWhatBoundsAWholeDocument) {
  const size_t value_bytes = coverwise::model::kMaxStringBytes;
  const size_t value_count = coverwise::model::kMaxAggregateStringBytes / value_bytes + 2;
  std::string input = R"({"parameters":[{"name":"a","values":[)";
  for (size_t i = 0; i < value_count; ++i) {
    if (i > 0) input += ',';
    input += '"';
    input.append(value_bytes - 8, 'x');
    input += std::to_string(1000000 + i);
    input += '"';
  }
  input += R"(]},{"name":"b","values":["0","1"]}]})";
  ASSERT_GT(input.size(), coverwise::model::kMaxAggregateStringBytes);
  ASSERT_LT(input.size(), coverwise::model::kMaxDocumentBytes);
  const std::string path = TempPath("aggregate_strings.json");
  WriteFile(path, input);

  const auto result = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(coverwise::model::AggregateBudgetExceededMessage()),
            std::string::npos)
      << result.stdout_text;
}

namespace {

/// @brief A suite of @p rows rows, each carrying two members of @p field_bytes.
std::string WideRowSuite(size_t rows, size_t field_bytes) {
  const std::string cell(field_bytes, 'x');
  std::string suite = "[";
  for (size_t row = 0; row < rows; ++row) {
    if (row > 0) suite += ',';
    suite += R"({"a":")" + cell + R"(","b":")" + cell + R"("})";
  }
  suite += ']';
  return suite;
}

/// @brief A model of @p count distinct values of @p bytes each, plus a narrow
///        second parameter so a suite over it is well formed.
coverwise::model::GenerateOptions WideModel(size_t count, size_t bytes) {
  std::vector<std::string> values;
  values.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    values.push_back(std::string(bytes - 8, 'x') + std::to_string(1000000 + index));
  }
  coverwise::model::GenerateOptions options;
  options.parameters.emplace_back("a", std::move(values));
  options.parameters.emplace_back("b", std::vector<std::string>{"1", "2"});
  options.strength = 2;
  return options;
}

/// @brief What every surface says when an input exceeds the aggregate string
///        budget, taken from the model layer rather than written down again.
///
/// A test that spells the sentence out is another copy of it, and two copies
/// disagreeing is the drift these assertions exist to catch. The TypeScript
/// surfaces compare against the same wording, so a change to it fails on both
/// sides at once instead of separating them.
std::string AggregateBudgetRefusal() { return coverwise::model::AggregateBudgetExceededMessage(); }

/// @brief A surface's reading of @p bytes of caller text, as the gate takes it.
///
/// The gate takes a total rather than a number so that no call path can supply
/// one without saying where it came from; a test standing in for a surface says
/// so here rather than reaching past the reader.
coverwise::model::ChargedText Charged(size_t bytes) {
  coverwise::model::ChargedTextReader reader;
  reader.Charge(bytes);
  return reader.total();
}

}  // namespace

// Row values are caller-supplied strings, so they are charged against the same
// documented aggregate budget the model's own strings are. A suite carrying
// more text than the budget allows is refused before the engine sees any of it,
// whichever command reads the rows.
TEST(CliInputBudgetTest, RowTextIsChargedAgainstTheAggregateStringBudget) {
  const std::string params_path = TempPath("row_budget_params.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                         R"({"name":"b","values":["1","2"]}]})");

  const size_t field_bytes = 60 * 1024;
  const size_t rows = coverwise::model::kMaxAggregateStringBytes / (2 * field_bytes) + 2;
  ASSERT_GT(rows * 2 * field_bytes, coverwise::model::kMaxAggregateStringBytes);
  ASSERT_LT(field_bytes, coverwise::model::kMaxStringBytes);
  const std::string rows_path = TempPath("row_budget_rows.json");
  WriteFile(rows_path, WideRowSuite(rows, field_bytes));

  const std::string expected = AggregateBudgetRefusal();
  const auto analyzed =
      RunCliCaptureStderr("analyze --params " + params_path + " --tests " + rows_path);
  EXPECT_EQ(analyzed.exit_code, 3) << analyzed.stdout_text.substr(0, 200);
  EXPECT_NE(analyzed.stdout_text.find(expected), std::string::npos)
      << analyzed.stdout_text.substr(0, 200);

  const auto extended = RunCliCaptureStderr("extend --existing " + rows_path + " " + params_path);
  EXPECT_EQ(extended.exit_code, 3) << extended.stdout_text.substr(0, 200);
  EXPECT_NE(extended.stdout_text.find(expected), std::string::npos)
      << extended.stdout_text.substr(0, 200);
}

// The budget bounds one invocation rather than one argument. Extend reads
// `seeds` from the model document and `existing` from a file of its own, and
// two half-sized suites are the same input dimension as one full-sized one.
TEST(CliInputBudgetTest, ExtendChargesSeedsAndExistingAgainstOneBudget) {
  const size_t field_bytes = 60 * 1024;
  const std::string first(field_bytes, 'a');
  const std::string second(field_bytes, 'b');
  const size_t rows = coverwise::model::kMaxAggregateStringBytes / (4 * field_bytes) + 1;
  ASSERT_LT(rows * 2 * field_bytes, coverwise::model::kMaxAggregateStringBytes);
  ASSERT_GT(2 * rows * 2 * field_bytes, coverwise::model::kMaxAggregateStringBytes);

  const std::string declared = R"({"name":"a","values":[")" + first + R"(",")" + second +
                               R"("]},{"name":"b","values":[")" + first + R"(",")" + second +
                               R"("]})";
  std::string suite = "[";
  for (size_t row = 0; row < rows; ++row) {
    if (row > 0) suite += ',';
    suite += R"({"a":")" + first + R"(","b":")" + second + R"("})";
  }
  suite += ']';

  const std::string existing_path = TempPath("one_budget_existing.json");
  WriteFile(existing_path, suite);
  const std::string without_seeds_path = TempPath("one_budget_model.json");
  WriteFile(without_seeds_path, "{\"parameters\":[" + declared + "]}");
  const std::string with_seeds_path = TempPath("one_budget_model_seeded.json");
  WriteFile(with_seeds_path, "{\"parameters\":[" + declared + "],\"seeds\":" + suite + "}");
  const std::string no_rows_path = TempPath("one_budget_no_rows.json");
  WriteFile(no_rows_path, "[]");

  // Either suite on its own fits.
  const auto existing_only =
      RunCliCaptureStderr("extend --existing " + existing_path + " " + without_seeds_path);
  EXPECT_EQ(existing_only.exit_code, 0) << existing_only.stdout_text.substr(0, 200);
  const auto seeds_only =
      RunCliCaptureStderr("extend --existing " + no_rows_path + " " + with_seeds_path);
  EXPECT_EQ(seeds_only.exit_code, 0) << seeds_only.stdout_text.substr(0, 200);

  // Together they do not, because they are one call's worth of row text.
  const auto both =
      RunCliCaptureStderr("extend --existing " + existing_path + " " + with_seeds_path);
  EXPECT_EQ(both.exit_code, 3) << both.stdout_text.substr(0, 200);
  EXPECT_NE(both.stdout_text.find(AggregateBudgetRefusal()), std::string::npos)
      << both.stdout_text.substr(0, 200);
}

// The budget covers an input, not one kind of string in it, and every surface
// says so in the same words. A model whose own strings are half the budget and
// a suite whose row text is the other half are each accepted alone and refused
// together — asked of the acceptance gate directly, which is the surface an
// embedding program uses, and of the command line, which is a process away.
// Neither the limit nor the sentence is written down here: the limit comes from
// the documented constant and the sentence from the model layer's own wording,
// which this also holds the gate's refusal to.
TEST(CliInputBudgetTest, ModelStringsAndRowTextShareOneBudgetOnEverySurface) {
  const size_t cell_bytes = 60 * 1024;
  const size_t cells = coverwise::model::kMaxAggregateStringBytes / (2 * cell_bytes) + 1;
  const size_t row_bytes = cells * cell_bytes;

  // The embedding surface: the gate itself, told how much row text the caller
  // read. A tiny model stands in for "no model text worth counting".
  EXPECT_TRUE(coverwise::model::AcceptOptions(WideModel(cells, cell_bytes),
                                              coverwise::model::ChargedText::None())
                  .ok());
  EXPECT_TRUE(coverwise::model::AcceptOptions(WideModel(2, 8), Charged(row_bytes)).ok());
  auto refused = coverwise::model::AcceptOptions(WideModel(cells, cell_bytes), Charged(row_bytes));
  ASSERT_FALSE(refused.ok());
  const std::string sentence = coverwise::model::SurfaceError(refused.error()).text();
  EXPECT_EQ(sentence, AggregateBudgetRefusal());
}

TEST(CliInputBudgetTest, ModelStringsAndRowTextShareOneBudget) {
  const size_t field_bytes = 60 * 1024;
  const size_t half_count = coverwise::model::kMaxAggregateStringBytes / (2 * field_bytes) + 1;
  ASSERT_LT(half_count * field_bytes, coverwise::model::kMaxAggregateStringBytes);
  ASSERT_GT(2 * half_count * field_bytes, coverwise::model::kMaxAggregateStringBytes);

  std::string wide_values;
  std::string wide_rows = "[";
  for (size_t index = 0; index < half_count; ++index) {
    // Distinct values, so the model is well formed rather than duplicated.
    const std::string value = std::string(field_bytes - 8, 'x') + std::to_string(1000000 + index);
    if (index > 0) {
      wide_values += ',';
      wide_rows += ',';
    }
    wide_values += '"' + value + '"';
    wide_rows += R"({"a":")" + value + R"("})";
  }
  wide_rows += ']';

  const std::string wide_params_path = TempPath("shared_budget_wide_params.json");
  WriteFile(wide_params_path, R"({"parameters":[{"name":"a","values":[)" + wide_values +
                                  R"(]},{"name":"b","values":["1","2"]}]})");
  const std::string narrow_params_path = TempPath("shared_budget_narrow_params.json");
  WriteFile(narrow_params_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                                R"({"name":"b","values":["1","2"]}]})");
  const std::string wide_rows_path = TempPath("shared_budget_wide_rows.json");
  WriteFile(wide_rows_path, wide_rows);
  const std::string narrow_rows_path = TempPath("shared_budget_narrow_rows.json");
  WriteFile(narrow_rows_path, R"([{"a":"x","b":"1"}])");

  const std::string budget_message = AggregateBudgetRefusal();

  const auto model_half =
      RunCliCaptureStderr("analyze --params " + wide_params_path + " --tests " + narrow_rows_path);
  EXPECT_EQ(model_half.stdout_text.find(budget_message), std::string::npos)
      << model_half.stdout_text.substr(0, 200);

  const auto row_half =
      RunCliCaptureStderr("analyze --params " + narrow_params_path + " --tests " + wide_rows_path);
  EXPECT_EQ(row_half.stdout_text.find(budget_message), std::string::npos)
      << row_half.stdout_text.substr(0, 200);

  // Byte for byte what the gate said, in the CLI's own envelope: a surface may
  // wrap the sentence but must not compose one of its own.
  const auto both =
      RunCliStderrOnly("analyze --params " + wide_params_path + " --tests " + wide_rows_path);
  EXPECT_EQ(both.exit_code, 3) << both.stdout_text.substr(0, 200);
  EXPECT_EQ(both.stdout_text, "error: " + budget_message + "\n");
}

// The budgets charge the text a surface read from its input, and that is the
// one respect in which embedding the library differs from running the command
// line — as docs/en/cpp-api.md and docs/ja/cpp-api.md say. A row written as
// JSON is text and is charged for every repeat of it; the same row built in C++
// is a list of value indices, so its text was never handed over. This drives
// both branches so the documented condition is the one that explains the
// difference, rather than a difference nobody wrote down.
TEST(CliInputBudgetTest, RowTextIsChargedToWhicheverSurfaceReadIt) {
  const size_t field_bytes = 60 * 1024;
  const std::string first(field_bytes, 'a');
  const std::string second(field_bytes, 'b');
  const size_t rows = coverwise::model::kMaxAggregateStringBytes / field_bytes + 2;
  ASSERT_LT(rows, coverwise::model::kMaxTests);
  // The model declares each wide value once, so the model's own strings are far
  // inside the budget and only the repetition in the rows can cross it.
  ASSERT_GT(rows * field_bytes, coverwise::model::kMaxAggregateStringBytes);

  const std::string params_path = TempPath("read_by_params.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":[")" + first + R"(",")" + second +
                             R"("]},{"name":"b","values":["1","2"]}]})");
  std::string suite = "[";
  for (size_t row = 0; row < rows; ++row) {
    if (row > 0) suite += ',';
    suite += R"({"a":")" + first + R"(","b":"1"})";
  }
  suite += ']';
  const std::string rows_path = TempPath("read_by_rows.json");
  WriteFile(rows_path, suite);

  // The command line read every one of those rows as text.
  const auto analyzed =
      RunCliCaptureStderr("analyze --params " + params_path + " --tests " + rows_path);
  EXPECT_EQ(analyzed.exit_code, 3) << analyzed.stdout_text.substr(0, 200);
  EXPECT_NE(analyzed.stdout_text.find(AggregateBudgetRefusal()), std::string::npos)
      << analyzed.stdout_text.substr(0, 200);

  // The same suite handed to the gate as value indices supplies no such text.
  coverwise::model::GenerateOptions options;
  options.parameters.emplace_back("a", std::vector<std::string>{first, second});
  options.parameters.emplace_back("b", std::vector<std::string>{"1", "2"});
  options.strength = 2;
  for (size_t row = 0; row < rows; ++row) {
    options.seeds.push_back(coverwise::model::TestCase{{0, 0}});
  }
  auto accepted =
      coverwise::model::AcceptOptions(std::move(options), coverwise::model::ChargedText::None());
  EXPECT_TRUE(accepted.ok()) << accepted.error().message;
}

// Row text that the options do carry is charged by exactly one of the two, and
// which one is what the charged total says. A suite whose row text alone is
// over the ceiling is refused by the embedding entry, which has no reader in
// front of it, and left to the reader that already counted it — so the same
// suite is refused once whichever way it arrives, and never twice.
TEST(CliInputBudgetTest, EitherTheReaderOrTheGateChargesRowText) {
  const size_t field_bytes = 60 * 1024;
  const size_t rows = coverwise::model::kMaxAggregateStringBytes / (2 * field_bytes) + 2;
  ASSERT_LT(rows, coverwise::model::kMaxTests);

  auto suite_of = []() {
    coverwise::model::GenerateOptions options;
    options.parameters.emplace_back("a", std::vector<std::string>{"x", "y"});
    options.parameters.emplace_back("b", std::vector<std::string>{"1", "2"});
    options.strength = 2;
    return options;
  };
  const size_t row_bytes = rows * 2 * field_bytes;
  ASSERT_GT(row_bytes, coverwise::model::kMaxAggregateStringBytes);

  // No reader: the caller's row text is here or it is charged nowhere.
  coverwise::model::GenerateOptions unread = suite_of();
  for (size_t row = 0; row < rows; ++row) {
    coverwise::model::TestCase drifted;
    drifted.values.assign(2, coverwise::model::kUnassigned);
    drifted.unresolved.assign(2, std::string(field_bytes, 'x'));
    unread.seeds.push_back(std::move(drifted));
  }
  auto refused =
      coverwise::model::AcceptOptions(std::move(unread), coverwise::model::ChargedText::None());
  ASSERT_FALSE(refused.ok());
  EXPECT_EQ(coverwise::model::SurfaceError(refused.error()).text(), AggregateBudgetRefusal());

  // A reader counted the same bytes: the verdict is the same, reached once.
  coverwise::model::ChargedTextReader reader;
  reader.Charge(row_bytes);
  auto also_refused = coverwise::model::AcceptOptions(suite_of(), reader.total());
  ASSERT_FALSE(also_refused.ok());
  EXPECT_EQ(coverwise::model::SurfaceError(also_refused.error()).text(), AggregateBudgetRefusal());

  // And the reader's count is not doubled by what the diagnostics kept: a suite
  // the reader found to fit still fits once the gate has walked the model.
  coverwise::model::GenerateOptions counted = suite_of();
  for (size_t row = 0; row < rows; ++row) {
    coverwise::model::TestCase drifted;
    drifted.values.assign(2, coverwise::model::kUnassigned);
    drifted.unresolved.assign(2, std::string(field_bytes, 'x'));
    counted.seeds.push_back(std::move(drifted));
  }
  coverwise::model::ChargedTextReader half;
  half.Charge(coverwise::model::kMaxAggregateStringBytes / 2);
  EXPECT_TRUE(coverwise::model::AcceptOptions(std::move(counted), half.total()).ok());
}

// ---------------------------------------------------------------------------
// Which kinds of string the budget charges
//
// Every surface charges the same set, and today they agree because each was
// written against the same description rather than because anything compares
// them. These fix the set by behaviour: a model sized to sit just under the
// budget, plus one instance of a single kind large enough to cross it if that
// kind were charged. Whichever way the verdict comes out names the kind, so a
// surface that starts charging row keys again fails here rather than moving a
// ceiling by an unexplained amount.
// ---------------------------------------------------------------------------

namespace {

/// @brief Distinct values whose UTF-8 bytes total exactly @p total_bytes.
std::vector<std::string> ValuesTotalling(size_t total_bytes) {
  constexpr size_t kChunkBytes = 32 * 1024;
  std::vector<std::string> values;
  size_t remaining = total_bytes;
  while (remaining > 0) {
    const size_t take = std::min(remaining, kChunkBytes);
    // A numeric prefix keeps the values distinct; the filler makes the length.
    std::string value = std::to_string(100000000 + values.size());
    value.append(take - value.size(), 'x');
    values.push_back(std::move(value));
    remaining -= take;
  }
  return values;
}

/// @brief Bytes the model below spends besides the bulk parameter's values:
///        the two parameter names and the narrow parameter's two values.
constexpr size_t kBudgetedModelOverhead = 4;

/// @brief A model whose charged strings sit exactly @p slack bytes below the
///        aggregate budget.
///
/// Everything in it is a charged kind, so the slack is the room a further
/// instance of any kind has to fit into: one larger than the slack crosses the
/// limit if and only if its kind is charged.
coverwise::model::GenerateOptions BudgetedModel(size_t slack) {
  coverwise::model::GenerateOptions options;
  options.parameters.emplace_back("a", ValuesTotalling(coverwise::model::kMaxAggregateStringBytes -
                                                       slack - kBudgetedModelOverhead));
  options.parameters.emplace_back("b", std::vector<std::string>{"1", "2"});
  options.strength = 2;
  return options;
}

/// @brief That model as a JSON document, so the command line reads the same one.
std::string BudgetedModelJson(const coverwise::model::GenerateOptions& options) {
  std::string document = R"({"parameters":[)";
  for (size_t index = 0; index < options.parameters.size(); ++index) {
    if (index > 0) document += ',';
    document += R"({"name":")" + options.parameters[index].name + R"(","values":[)";
    for (size_t value = 0; value < options.parameters[index].values.size(); ++value) {
      if (value > 0) document += ',';
      document += '"' + options.parameters[index].values[value] + '"';
    }
    document += "]}";
  }
  return document + "]}";
}

/// @brief Slack small enough that a single instance of a kind can cross it and
///        stay inside the per-string limit, and large enough that the keys a
///        row needs to carry that instance stay well under it.
constexpr size_t kKindSlack = 2048;

}  // namespace

TEST(BudgetedKindsTest, TheGateChargesEveryKindOfModelString) {
  const std::string oversized(kKindSlack + 1, 'z');
  const std::string budget_refusal = coverwise::model::AggregateBudgetExceededMessage();

  // The arithmetic first: the model fits, it fits with the slack exactly spent,
  // and one byte more than the slack is refused for the budget and nothing else.
  EXPECT_TRUE(coverwise::model::AcceptOptions(BudgetedModel(kKindSlack),
                                              coverwise::model::ChargedText::None())
                  .ok());
  EXPECT_TRUE(coverwise::model::AcceptOptions(BudgetedModel(kKindSlack), Charged(kKindSlack)).ok());
  auto over = coverwise::model::AcceptOptions(BudgetedModel(kKindSlack), Charged(kKindSlack + 1));
  ASSERT_FALSE(over.ok());
  EXPECT_EQ(coverwise::model::SurfaceError(over.error()).text(), budget_refusal);

  // One oversized instance per kind of model string. Each is refused, and for
  // the budget rather than for a rule of its own — the gate weighs the budget
  // before it looks at whether a sub-model or a weight names a real parameter.
  std::vector<std::pair<std::string, coverwise::model::GenerateOptions>> by_kind;

  auto parameter_name = BudgetedModel(kKindSlack);
  parameter_name.parameters.emplace_back(oversized, std::vector<std::string>{"7", "8"});
  by_kind.emplace_back("parameter name", std::move(parameter_name));

  auto value = BudgetedModel(kKindSlack);
  value.parameters[1].values.push_back(oversized);
  by_kind.emplace_back("value", std::move(value));

  auto alias = BudgetedModel(kKindSlack);
  alias.parameters[1].set_aliases({{oversized}, {}});
  by_kind.emplace_back("alias", std::move(alias));

  auto equivalence_class = BudgetedModel(kKindSlack);
  equivalence_class.parameters[1].set_equivalence_classes({oversized, "other"});
  by_kind.emplace_back("class name", std::move(equivalence_class));

  auto constraint = BudgetedModel(kKindSlack);
  constraint.constraint_expressions.push_back(oversized);
  by_kind.emplace_back("constraint expression", std::move(constraint));

  auto sub_model = BudgetedModel(kKindSlack);
  sub_model.sub_models.push_back({{oversized}, 1});
  by_kind.emplace_back("sub-model parameter name", std::move(sub_model));

  auto weight_parameter = BudgetedModel(kKindSlack);
  weight_parameter.weights.entries[oversized]["1"] = 2.0;
  by_kind.emplace_back("weight parameter name", std::move(weight_parameter));

  auto weight_value = BudgetedModel(kKindSlack);
  weight_value.weights.entries["b"][oversized] = 2.0;
  by_kind.emplace_back("weight value name", std::move(weight_value));

  for (auto& [kind, options] : by_kind) {
    auto refused =
        coverwise::model::AcceptOptions(std::move(options), coverwise::model::ChargedText::None());
    ASSERT_FALSE(refused.ok()) << kind;
    EXPECT_EQ(coverwise::model::SurfaceError(refused.error()).text(), budget_refusal) << kind;
  }
}

TEST(BudgetedKindsTest, TheReaderChargesRowValuesAndNothingElseInARow) {
  const std::string model_path = TempPath("kinds_model.json");
  WriteFile(model_path, BudgetedModelJson(BudgetedModel(kKindSlack)));
  const std::string budget_refusal = coverwise::model::AggregateBudgetExceededMessage();

  const auto analyze = [&](const std::string& suite, const std::string& name) {
    const std::string path = TempPath(name);
    WriteFile(path, suite);
    return RunCliCaptureStderr("analyze --params " + model_path + " --tests " + path);
  };
  const auto rows = [](size_t count, const std::string& member) {
    std::string suite = "[";
    for (size_t row = 0; row < count; ++row) {
      if (row > 0) suite += ',';
      suite += '{' + member + '}';
    }
    return suite + ']';
  };

  // A string row value is the caller's own text and is charged: four of them
  // over the slack cross the limit.
  const std::string wide_value(kKindSlack / 3, 's');
  const auto strings = analyze(rows(4, R"("a":")" + wide_value + R"(")"), "kinds_strings.json");
  EXPECT_EQ(strings.exit_code, 3) << strings.stdout_text.substr(0, 200);
  EXPECT_NE(strings.stdout_text.find(budget_refusal), std::string::npos)
      << strings.stdout_text.substr(0, 200);

  // A row key is a parameter name, charged once as a model string. Charging it
  // again per row would make the budget shrink with the parameter count.
  const std::string wide_key(kKindSlack / 3, 'k');
  const auto keys = analyze(rows(4, '"' + wide_key + R"(":1)"), "kinds_keys.json");
  EXPECT_EQ(keys.stdout_text.find(budget_refusal), std::string::npos)
      << keys.stdout_text.substr(0, 200);

  // A string under a key that names no parameter is still the caller's own
  // text, handed over and held for as long as the row is read, so it is charged
  // like any other row value. A surface that dropped it before counting would
  // accept a suite another surface refuses, on the strength of nothing but
  // whether the model happened to have somewhere to put it.
  const auto undeclared =
      analyze(rows(4, R"("undeclared":")" + wide_value + R"(")"), "kinds_undeclared.json");
  EXPECT_NE(undeclared.stdout_text.find(budget_refusal), std::string::npos)
      << undeclared.stdout_text.substr(0, 200);

  // Numbers and booleans are rendered by the engine rather than supplied as
  // text, so they cost nothing however many of them a suite carries.
  const auto numbers = analyze(rows(300, R"("a":1234567890123.5)"), "kinds_numbers.json");
  EXPECT_EQ(numbers.stdout_text.find(budget_refusal), std::string::npos)
      << numbers.stdout_text.substr(0, 200);

  const auto booleans = analyze(rows(800, R"("a":true)"), "kinds_booleans.json");
  EXPECT_EQ(booleans.stdout_text.find(budget_refusal), std::string::npos)
      << booleans.stdout_text.substr(0, 200);

  // Text that does not resolve is kept for the diagnostic, and is charged where
  // it is read rather than again wherever it is kept: this row fits once and
  // would not fit twice.
  const std::string unresolved((kKindSlack * 3) / 4, 'u');
  const auto drifted = analyze(rows(1, R"("a":")" + unresolved + R"(")"), "kinds_drifted.json");
  EXPECT_EQ(drifted.stdout_text.find(budget_refusal), std::string::npos)
      << drifted.stdout_text.substr(0, 200);
}

// A recorded suite drifts from the model it was written against — a value gets
// renamed, a parameter is added. Filling the gap in the model is what extend is
// for, so a drifted row is kept exactly as written, reported as excluded from
// coverage, and left out of the coverage figure. The same input produces the
// same three things on the JavaScript surfaces.
TEST(CliExtendTest, PreservesAnExistingRowCarryingAValueTheModelNoLongerHas) {
  const std::string model_path = TempPath("extend_model.json");
  WriteFile(model_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                        R"({"name":"b","values":["1","2"]}]})");
  const std::string existing_path = TempPath("extend_existing_unknown.json");
  WriteFile(existing_path, R"([{"a":"removed","b":"1"}])");

  const auto result = RunCliCaptureStderr("extend --existing " + existing_path + " " + model_path);
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("tests":[{"a":"removed","b":"1"})"), std::string::npos)
      << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("Existing test 0 preserved but excluded from coverage"),
            std::string::npos)
      << result.stdout_text;
  // The warning is the caller's only account of why the row was left out, so it
  // names the value they wrote. The unassigned sentinel is an implementation
  // detail of the index vector and tells them nothing about their own input.
  EXPECT_NE(result.stdout_text.find("value 'removed' is not declared by parameter a"),
            std::string::npos)
      << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find("4294967295"), std::string::npos) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("coverage":1)"), std::string::npos) << result.stdout_text;
}

TEST(CliExtendTest, PreservesAnExistingRowMissingAParameter) {
  const std::string model_path = TempPath("extend_model_missing.json");
  WriteFile(model_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                        R"({"name":"b","values":["1","2"]}]})");
  const std::string existing_path = TempPath("extend_existing_missing.json");
  WriteFile(existing_path, R"([{"a":"x"}])");

  const auto result = RunCliCaptureStderr("extend --existing " + existing_path + " " + model_path);
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("tests":[{"a":"x"},)"), std::string::npos)
      << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("Existing test 0 preserved but excluded from coverage"),
            std::string::npos)
      << result.stdout_text;
  // A row that carried no member for b has no text of the caller's to quote, so
  // the parameter alone identifies what is missing.
  EXPECT_NE(result.stdout_text.find("no value recorded for parameter b"), std::string::npos)
      << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find("4294967295"), std::string::npos) << result.stdout_text;
}

// A seed is asserted to be a real test case for this model, so its key set must
// match the declared parameter names exactly. An extra member means the caller
// is describing something the model does not have.
TEST(CliGenerateTest, RejectsASeedRowCarryingAnUndeclaredMember) {
  const std::string path = TempPath("seed_extra_member.json");
  WriteFile(path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                  R"({"name":"b","values":["1","2"]}],)"
                  R"("seeds":[{"a":"x","b":"1","note":"hi"}]})");

  const auto result = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("unknown parameter 'note'"), std::string::npos)
      << result.stdout_text;
}

// Integer expansion steps by one, so any other step describes a value set the
// engine will not produce.
TEST(CliGenerateTest, RejectsAnIntegerBoundaryStepOtherThanOne) {
  const std::string path = TempPath("integer_step_five.json");
  WriteFile(path,
            R"({"parameters":[{"name":"n","type":"integer","range":[0,10],"step":5,"values":[]},)"
            R"({"name":"m","values":["a","b"]}]})");

  const auto result = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("Integer boundary step must be 1 for parameter n"),
            std::string::npos)
      << result.stdout_text;
}

TEST(CliGenerateTest, IntegerBoundaryStepOfOneExpandsTheDocumentedValueSet) {
  const std::string path = TempPath("integer_step_one.json");
  WriteFile(path,
            R"({"parameters":[{"name":"n","type":"integer","range":[0,10],"step":1,"values":[]},)"
            R"({"name":"m","values":["a","b"]}]})");

  const auto result = RunCli("generate " + path);
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text;
  for (const char* value :
       {R"("n":"-1")", R"("n":"0")", R"("n":"1")", R"("n":"9")", R"("n":"10")", R"("n":"11")"}) {
    EXPECT_NE(result.stdout_text.find(value), std::string::npos) << value << result.stdout_text;
  }
  EXPECT_EQ(result.stdout_text.find(R"("n":"5")"), std::string::npos) << result.stdout_text;
}

// A boundary parameter may spell out only the values it wants marked invalid and
// leave the valid ones to the range: the point of the range is to supply them.
TEST(CliGenerateTest, AcceptsABoundaryParameterWhoseOnlyDeclaredValueIsInvalid) {
  const std::string path = TempPath("boundary_invalid_sentinel.json");
  WriteFile(path, R"({"parameters":[{"name":"age","type":"integer","range":[0,10],)"
                  R"("values":[{"value":999,"invalid":true}]},)"
                  R"({"name":"mode","values":["a","b"]}]})");

  const auto result = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("negativeTests":[{"age":"999")"), std::string::npos)
      << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("age":"11")"), std::string::npos) << result.stdout_text;
}

// A boundary shape the reader cannot convert is an error on every surface. The
// path that used to matter here is the one that silently skipped expansion and
// generated over the declared values instead.
TEST(CliGenerateTest, RejectsABoundaryRangeThatIsNotNumeric) {
  const std::string path = TempPath("boundary_bad_range.json");
  WriteFile(path, R"({"parameters":[{"name":"n","type":"integer","range":["0","10"],"values":[]},)"
                  R"({"name":"m","values":["a","b"]}]})");

  const auto result = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find(R"("tests":)"), std::string::npos) << result.stdout_text;
}

// Every way a row can fail to match the model goes down one path: the row is
// excluded from coverage and reported in invalidTests with its index. A single
// drifted row costs its own coverage, not the whole report.
TEST(CliAnalyzeTest, ReportsARowMissingAParameterInInvalidTests) {
  const std::string params_path = TempPath("analyze_params_missing.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                         R"({"name":"b","values":["1","2"]}]})");
  const std::string tests_path = TempPath("analyze_tests_missing.json");
  WriteFile(tests_path, R"([{"a":"x"},{"a":"y","b":"2"}])");

  const auto result = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("coverageRatio":0.25)"), std::string::npos)
      << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("invalidTests":[{"testIndex":0)"), std::string::npos)
      << result.stdout_text;
}

TEST(CliAnalyzeTest, ReportsARowWithAnOutOfDomainValueInInvalidTests) {
  const std::string params_path = TempPath("analyze_params_unknown.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                         R"({"name":"b","values":["1","2"]}]})");
  const std::string tests_path = TempPath("analyze_tests_unknown.json");
  WriteFile(tests_path, R"([{"a":"zzz","b":"1"},{"a":"y","b":"2"}])");

  const auto result = RunCli("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("coverageRatio":0.25)"), std::string::npos)
      << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"("invalidTests":[{"testIndex":0)"), std::string::npos)
      << result.stdout_text;
}

// A recorded row is described back to the caller in the caller's own terms. The
// two commands that consume such a suite read it with the same reader, so a row
// carrying a value the model no longer declares has to be reported by that
// value on both — naming the parameter alone leaves the caller unable to tell
// which member of the row drifted.
TEST(CliRecordedRowTest, TheRejectionReasonNamesTheValueTheRowCarried) {
  const std::string model_path = TempPath("recorded_row_model.json");
  WriteFile(model_path, R"({"parameters":[{"name":"browser","values":["chrome","firefox"]},)"
                        R"({"name":"os","values":["linux","mac"]}]})");
  const std::string rows_path = TempPath("recorded_row_suite.json");
  WriteFile(rows_path, R"([{"browser":"edge","os":"linux"},{"browser":"chrome","os":"mac"},)"
                       R"({"browser":"chrome","os":"linux"},{"browser":"firefox","os":"linux"},)"
                       R"({"browser":"firefox","os":"mac"}])");

  const auto analyzed = RunCli("analyze --params " + model_path + " --tests " + rows_path);
  EXPECT_EQ(analyzed.exit_code, 3) << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find("edge"), std::string::npos) << analyzed.stdout_text;
  EXPECT_EQ(analyzed.stdout_text.find("missing value"), std::string::npos) << analyzed.stdout_text;

  const auto extended = RunCli("extend --existing " + rows_path + " " + model_path);
  EXPECT_EQ(extended.exit_code, 0) << extended.stdout_text;
  EXPECT_NE(extended.stdout_text.find("value 'edge' is not declared by parameter browser"),
            std::string::npos)
      << extended.stdout_text;
}

// A suite at the documented row limit is accepted. The number that decides is
// the documented row count, not the byte length of the document carrying it —
// the same suite is accepted by the TypeScript surfaces, which never see a byte
// stream at all.
TEST(CliAnalyzeTest, AcceptsASuiteAtTheDocumentedRowLimit) {
  const std::string params_path = TempPath("analyze_params_bulk.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                         R"({"name":"b","values":["1","2"]},)"
                         R"({"name":"c","values":["p","q"]}]})");

  std::string tests = "[";
  const char* a_values[] = {"x", "y"};
  const char* b_values[] = {"1", "2"};
  const char* c_values[] = {"p", "q"};
  for (size_t i = 0; i < coverwise::model::kMaxTests; ++i) {
    if (i > 0) tests += ',';
    tests += R"({"a":")";
    tests += a_values[i % 2];
    tests += R"(","b":")";
    tests += b_values[(i / 2) % 2];
    tests += R"(","c":")";
    tests += c_values[(i / 4) % 2];
    tests += R"("})";
  }
  tests += ']';
  ASSERT_GT(tests.size(), coverwise::model::kMaxAggregateStringBytes);
  const std::string tests_path = TempPath("analyze_tests_bulk.json");
  WriteFile(tests_path, tests);

  const auto result =
      RunCliCaptureStderr("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 0) << result.stdout_text.substr(0, 400);
  EXPECT_NE(result.stdout_text.find(R"("coverageRatio":1)"), std::string::npos)
      << result.stdout_text.substr(0, 400);
}

TEST(CliAnalyzeTest, RejectsASuiteBeyondTheDocumentedRowLimit) {
  const std::string params_path = TempPath("analyze_params_overlimit.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["x","y"]}]})");

  std::string tests = "[";
  for (size_t i = 0; i <= coverwise::model::kMaxTests; ++i) {
    if (i > 0) tests += ',';
    tests += i % 2 == 0 ? R"({"a":"x"})" : R"({"a":"y"})";
  }
  tests += ']';
  const std::string tests_path = TempPath("analyze_tests_overlimit.json");
  WriteFile(tests_path, tests);

  const auto result =
      RunCliCaptureStderr("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text.substr(0, 400);
  EXPECT_NE(result.stdout_text.find("exceed maximum of"), std::string::npos)
      << result.stdout_text.substr(0, 400);
}

// ---------------------------------------------------------------------------
// The surface representation of a failure.
// ---------------------------------------------------------------------------

namespace {

using coverwise::model::Error;
using coverwise::model::ExitStatus;
using coverwise::model::SurfaceError;

// What a caller is shown about a failure can only be derived from the failure.
// Neither an exit code nor a message can be supplied at the call site, so a
// subcommand cannot quietly answer differently from the rest of the CLI.
static_assert(!std::is_default_constructible_v<SurfaceError>,
              "a surfaced failure must come from a structured error");
static_assert(!std::is_constructible_v<SurfaceError, int>,
              "an exit code must not be constructible into a surfaced failure");
static_assert(!std::is_constructible_v<SurfaceError, Error::Code>,
              "a bare code carries no message and must not surface on its own");
static_assert(!std::is_constructible_v<SurfaceError, std::string>,
              "an error string must not be composed at the call site");
static_assert(!std::is_constructible_v<SurfaceError, const char*>,
              "an error string must not be composed at the call site");
static_assert(std::is_constructible_v<SurfaceError, const Error&>,
              "a structured error is the one thing that surfaces");

static_assert(!std::is_default_constructible_v<ExitStatus>,
              "an exit status is either success or a surfaced failure");
static_assert(!std::is_constructible_v<ExitStatus, int>,
              "an operation must not name its own exit code");
static_assert(!std::is_constructible_v<ExitStatus, Error>,
              "an exit status must be surfaced before it can be returned");
static_assert(std::is_constructible_v<ExitStatus, const SurfaceError&>,
              "a surfaced failure is returnable as an exit status");

}  // namespace

TEST(SurfaceErrorTest, TheExitCodeMappingIsTotalAndDocumented) {
  EXPECT_EQ(SurfaceError(Error{Error::Code::kOk, "", ""}).exit_code(), 0);
  EXPECT_EQ(SurfaceError(Error{Error::Code::kConstraintError, "", ""}).exit_code(), 1);
  EXPECT_EQ(SurfaceError(Error{Error::Code::kInsufficientCoverage, "", ""}).exit_code(), 2);
  EXPECT_EQ(SurfaceError(Error{Error::Code::kInvalidInput, "", ""}).exit_code(), 3);
  // A tuple explosion is invalid input to a caller. Its enum value is 4, which
  // is not one of the documented exit codes and must never reach a shell.
  EXPECT_EQ(SurfaceError(Error{Error::Code::kTupleExplosion, "", ""}).exit_code(), 3);
}

// Success is the one exit code a failure must never be given. A value outside
// the enumeration is a failure of unknown kind, and answering 0 for it would
// tell a caller gating on the exit code that nothing went wrong — the one
// mistake nothing downstream can recover from.
TEST(SurfaceErrorTest, OnlyTheOkCodeSurfacesAsSuccess) {
  for (int raw = 0; raw <= 8; ++raw) {
    const auto code = static_cast<Error::Code>(raw);
    const int exit_code = SurfaceError(Error{code, "", ""}).exit_code();
    if (code == Error::Code::kOk) {
      EXPECT_EQ(exit_code, 0) << "raw " << raw;
    } else {
      EXPECT_NE(exit_code, 0) << "raw " << raw;
    }
  }
  EXPECT_EQ(SurfaceError(Error{static_cast<Error::Code>(99), "", ""}).exit_code(), 3);
}

TEST(SurfaceErrorTest, AnAbsentDetailLeavesNoSeparatorBehind) {
  const Error without_detail{Error::Code::kInvalidInput, "At least one parameter is required", ""};
  EXPECT_EQ(SurfaceError(without_detail).text(), "At least one parameter is required");

  const Error with_detail{Error::Code::kInvalidInput, "Invalid strength", "strength must be >= 1"};
  EXPECT_EQ(SurfaceError(with_detail).text(), "Invalid strength: strength must be >= 1");
}

// A model-layer rejection is shown as the surfaced form of that very Error, not
// as text a reader assembled on the way out. Every command that reads the model
// document shares the reader, so all of them say the same thing.
TEST(CliModelReaderTest, AModelRejectionIsTheSurfacedFormOfItsError) {
  const std::string model_path = TempPath("reader_boundary_model.json");
  WriteFile(model_path, R"({"parameters":[{"name":"a","type":"integer","range":[5,1],"values":[]},)"
                        R"({"name":"b","values":["0","1"]}]})");
  const std::string rows_path = TempPath("reader_boundary_rows.json");
  WriteFile(rows_path, R"([{"a":"1","b":"0"}])");

  const std::string expected =
      "error: " +
      SurfaceError(Error{Error::Code::kInvalidInput,
                         "Boundary range must be finite and ordered for parameter a", ""})
          .text() +
      "\n";

  for (const std::string& args : {"generate " + model_path, "stats " + model_path,
                                  "extend --existing " + rows_path + " " + model_path}) {
    const auto result = RunCliStderrOnly(args);
    EXPECT_EQ(result.exit_code, 3) << args;
    EXPECT_EQ(result.stdout_text, expected) << args;
  }
}

// The detail is the half of an Error that says which of the caller's numbers
// were in conflict. A reader that hands its caller a bare string has nowhere to
// put it, so the caller is told a rule was broken without being told by what.
TEST(CliModelReaderTest, ADetailedRejectionReachesTheCallerWhole) {
  const std::string model_path = TempPath("reader_detail_model.json");
  WriteFile(model_path,
            R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],)"
            R"("strength":5})");

  const std::string expected = "error: " +
                               SurfaceError(Error{Error::Code::kInvalidInput,
                                                  "Strength must be between 1 and parameter count",
                                                  "strength=5, parameters=2"})
                                   .text() +
                               "\n";

  const auto result = RunCliStderrOnly("generate " + model_path);
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_EQ(result.stdout_text, expected);
}

TEST(CliExitCodeTest, OneMalformedConstraintExitsTheSameFromEverySubcommand) {
  const std::string model = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "constraints": ["unknown = 0"]
  })";
  const std::string model_path = TempPath("shared_bad_constraint.json");
  WriteFile(model_path, model);
  const std::string rows_path = TempPath("shared_bad_constraint_rows.json");
  WriteFile(rows_path, R"([{"a":"0","b":"0"}])");

  const auto generated = RunCliCaptureStderr("generate " + model_path);
  const auto stats = RunCliCaptureStderr("stats " + model_path);
  const auto extended = RunCliCaptureStderr("extend --existing " + rows_path + " " + model_path);
  const auto analyzed =
      RunCliCaptureStderr("analyze --params " + model_path + " --tests " + rows_path);

  EXPECT_EQ(generated.exit_code, 1) << generated.stdout_text;
  EXPECT_EQ(stats.exit_code, 1) << stats.stdout_text;
  EXPECT_EQ(extended.exit_code, 1) << extended.stdout_text;
  EXPECT_EQ(analyzed.exit_code, 1) << analyzed.stdout_text;
}

TEST(CliAnalyzeTest, NamesTheConstraintExpressionThatFailedToParse) {
  const std::string params_path = TempPath("analyze_named_constraint_params.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["0","1"]}]})");
  const std::string tests_path = TempPath("analyze_named_constraint_tests.json");
  WriteFile(tests_path, R"([{"a":"0"},{"a":"1"}])");
  const std::string constraints_path = TempPath("analyze_named_constraint_list.json");
  WriteFile(constraints_path, R"({"constraints":["a = 0","a = ","a != 1"]})");

  const auto result = RunCliCaptureStderr("analyze --params " + params_path + " --tests " +
                                          tests_path + " --constraints " + constraints_path);
  EXPECT_EQ(result.exit_code, 1) << result.stdout_text;
  // Which of the three expressions failed is the whole diagnostic; a bare parse
  // message leaves the caller bisecting the file by hand.
  EXPECT_NE(result.stdout_text.find(R"(Invalid constraint "a = ")"), std::string::npos)
      << result.stdout_text;
}

TEST(CliAnalyzeTest, AConstraintErrorWithoutADetailEndsWithoutASeparator) {
  const std::string params_path = TempPath("analyze_no_detail_params.json");
  WriteFile(params_path,
            R"({"parameters":[{"name":"a","values":["0","1"]}],"constraints":["a = \"0"]})");
  const std::string tests_path = TempPath("analyze_no_detail_tests.json");
  WriteFile(tests_path, R"([{"a":"0"}])");

  const auto result =
      RunCliCaptureStderr("analyze --params " + params_path + " --tests " + tests_path);
  EXPECT_EQ(result.exit_code, 1) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("Unterminated string literal"), std::string::npos)
      << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find(": \n"), std::string::npos) << result.stdout_text;
}

TEST(CliGenerateTest, AWarningForAnErrorWithoutADetailEndsWithoutASeparator) {
  const std::string path = TempPath("generate_no_detail.json");
  WriteFile(path,
            R"({"parameters":[{"name":"a","values":["0","1"]},{"name":"b","values":["0","1"]}],)"
            R"("constraints":["a = \"0"]})");

  const auto result = RunCli("generate " + path);
  EXPECT_EQ(result.exit_code, 1) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find(R"(Unterminated string literal starting at position 4")"),
            std::string::npos)
      << result.stdout_text;
  EXPECT_EQ(result.stdout_text.find("Unterminated string literal starting at position 4: "),
            std::string::npos)
      << result.stdout_text;
}

TEST(CliAnalyzeTest, MeasuresTheInteractionStrengthTheModelDeclares) {
  const std::string model = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]},
      {"name": "c", "values": ["0", "1"]}
    ],
    "strength": 3
  })";
  const std::string model_path = TempPath("analyze_strength_model.json");
  WriteFile(model_path, model);

  const auto generated = RunCli("generate " + model_path);
  ASSERT_EQ(generated.exit_code, 0) << generated.stdout_text;
  const std::string tests_path = TempPath("analyze_strength_tests.json");
  WriteFile(tests_path, generated.stdout_text);

  // The documented pipeline hands one model to both subcommands, so analyze has
  // to measure the universe that model defines: C(3,3) * 2^3 = 8 triples, not
  // the 12 pairs a hardcoded pairwise measurement would report.
  const auto analyzed =
      RunCliCaptureStderr("analyze --params " + model_path + " --tests " + tests_path);
  EXPECT_EQ(analyzed.exit_code, 0) << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find(R"("totalTuples":8)"), std::string::npos)
      << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find(R"("coverageRatio":1)"), std::string::npos)
      << analyzed.stdout_text;
}

TEST(CliAnalyzeTest, AnExplicitStrengthFlagWinsOverTheModel) {
  const std::string model = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]},
      {"name": "c", "values": ["0", "1"]}
    ],
    "strength": 3
  })";
  const std::string model_path = TempPath("analyze_strength_override_model.json");
  WriteFile(model_path, model);

  const auto generated = RunCli("generate " + model_path);
  ASSERT_EQ(generated.exit_code, 0) << generated.stdout_text;
  const std::string tests_path = TempPath("analyze_strength_override_tests.json");
  WriteFile(tests_path, generated.stdout_text);

  // C(3,2) * 2^2 = 12 pairs: the flag is the caller's choice for this run.
  const auto analyzed = RunCliCaptureStderr("analyze --params " + model_path + " --tests " +
                                            tests_path + " --strength 2");
  EXPECT_EQ(analyzed.exit_code, 0) << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find(R"("totalTuples":12)"), std::string::npos)
      << analyzed.stdout_text;
}

TEST(CliAnalyzeTest, GenerateAndAnalyzeAgreeOnASingleParameterUniverse) {
  const std::string model = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "strength": 1
  })";
  const std::string model_path = TempPath("analyze_strength_one_model.json");
  WriteFile(model_path, model);

  const auto generated = RunCli("generate " + model_path);
  ASSERT_EQ(generated.exit_code, 0) << generated.stdout_text;
  EXPECT_NE(generated.stdout_text.find(R"("coverage":1)"), std::string::npos)
      << generated.stdout_text;
  const std::string tests_path = TempPath("analyze_strength_one_tests.json");
  WriteFile(tests_path, generated.stdout_text);

  const auto analyzed =
      RunCliCaptureStderr("analyze --params " + model_path + " --tests " + tests_path);
  EXPECT_EQ(analyzed.exit_code, 0) << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find(R"("coverageRatio":1)"), std::string::npos)
      << analyzed.stdout_text;
}

TEST(CliAnalyzeTest, RefusesAModelWhoseSubModelsItCannotMeasure) {
  const std::string model = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]},
      {"name": "c", "values": ["0", "1"]}
    ],
    "subModels": [{"parameters": ["a", "b"], "strength": 2}]
  })";
  const std::string model_path = TempPath("analyze_submodels_model.json");
  WriteFile(model_path, model);
  const std::string tests_path = TempPath("analyze_submodels_tests.json");
  WriteFile(tests_path, R"([{"a":"0","b":"0","c":"0"}])");

  // Dropping the sub-models would report a ratio for a universe the caller
  // never described, so the refusal has to come before any report is written.
  const auto analyzed =
      RunCliCaptureStderr("analyze --params " + model_path + " --tests " + tests_path);
  EXPECT_EQ(analyzed.exit_code, 3) << analyzed.stdout_text;
  EXPECT_NE(analyzed.stdout_text.find("subModels"), std::string::npos) << analyzed.stdout_text;
  EXPECT_EQ(analyzed.stdout_text.find(R"("coverageRatio")"), std::string::npos)
      << analyzed.stdout_text;
}

TEST(CliAnalyzeTest, RejectsAConstraintsFileThatIsNotAConstraintList) {
  const std::string model = R"({
    "parameters": [
      {"name": "a", "values": ["0", "1"]},
      {"name": "b", "values": ["0", "1"]}
    ],
    "constraints": ["IF a = 0 THEN b = 0"]
  })";
  const std::string model_path = TempPath("analyze_null_constraints_model.json");
  WriteFile(model_path, model);

  const auto generated = RunCli("generate " + model_path);
  ASSERT_EQ(generated.exit_code, 0) << generated.stdout_text;
  const std::string tests_path = TempPath("analyze_null_constraints_tests.json");
  WriteFile(tests_path, generated.stdout_text);

  // Without the file the model's own constraints apply and the suite covers the
  // feasible universe.
  const auto without_file =
      RunCliCaptureStderr("analyze --params " + model_path + " --tests " + tests_path);
  EXPECT_EQ(without_file.exit_code, 0) << without_file.stdout_text;
  EXPECT_NE(without_file.stdout_text.find(R"("coverageRatio":1)"), std::string::npos)
      << without_file.stdout_text;

  // `jq '.constraints'` writes bare null for a model that declares none. Taking
  // that as "no constraints" would erase the model's own set and turn a passing
  // gate into an unexplained shortfall.
  const std::string null_path = TempPath("analyze_null_constraints.json");
  WriteFile(null_path, "null");
  const auto with_null = RunCliCaptureStderr("analyze --params " + model_path + " --tests " +
                                             tests_path + " --constraints " + null_path);
  EXPECT_EQ(with_null.exit_code, 3) << with_null.stdout_text;
  EXPECT_EQ(with_null.stdout_text.find(R"("coverageRatio")"), std::string::npos)
      << with_null.stdout_text;
}

TEST(CliAnalyzeTest, RejectsAStrengthFlagBelowOne) {
  const std::string params_path = TempPath("analyze_zero_strength_params.json");
  WriteFile(params_path, R"({"parameters":[{"name":"a","values":["0","1"]}]})");
  const std::string tests_path = TempPath("analyze_zero_strength_tests.json");
  WriteFile(tests_path, R"([{"a":"0"}])");

  const auto result = RunCliCaptureStderr("analyze --params " + params_path + " --tests " +
                                          tests_path + " --strength 0");
  EXPECT_EQ(result.exit_code, 3) << result.stdout_text;
  EXPECT_NE(result.stdout_text.find("--strength must be a positive integer"), std::string::npos)
      << result.stdout_text;
}

TEST(CliExtendTest, HandsBackAnExistingRowExactlyAsItWasSupplied) {
  const std::string model = R"({
    "parameters": [
      {"name": "browser", "values": [{"value": "Chromium", "aliases": ["Chrome"]}, "Firefox"]},
      {"name": "os", "values": ["win", "mac"]}
    ]
  })";
  const std::string model_path = TempPath("extend_alias_model.json");
  WriteFile(model_path, model);

  // Rendering a preserved row from its value index substitutes the primary
  // value for the alias the caller wrote, so a suite fed back to a runner no
  // longer matches on the strings that were submitted.
  const std::string alias_rows_path = TempPath("extend_alias_rows.json");
  WriteFile(alias_rows_path, R"([{"browser":"Chrome","os":"win"}])");
  const auto by_alias = RunCli("extend --existing " + alias_rows_path + " " + model_path);
  EXPECT_EQ(by_alias.exit_code, 0) << by_alias.stdout_text;
  EXPECT_NE(by_alias.stdout_text.find(R"("tests":[{"browser":"Chrome","os":"win"})"),
            std::string::npos)
      << by_alias.stdout_text;

  const std::string primary_rows_path = TempPath("extend_primary_rows.json");
  WriteFile(primary_rows_path, R"([{"browser":"Chromium","os":"win"}])");
  const auto by_primary = RunCli("extend --existing " + primary_rows_path + " " + model_path);
  EXPECT_EQ(by_primary.exit_code, 0) << by_primary.stdout_text;
  EXPECT_NE(by_primary.stdout_text.find(R"("tests":[{"browser":"Chromium","os":"win"})"),
            std::string::npos)
      << by_primary.stdout_text;
}

// ---------------------------------------------------------------------------
// Resolving a value name the caller wrote.
// ---------------------------------------------------------------------------

namespace {

/// @brief The parameters every case below resolves names against.
///
/// Every declared spelling is mixed case, so a name written in another case is
/// something the command has to fold rather than something it matches by luck.
constexpr const char* kMixedCaseParameters =
    R"("parameters":[{"name":"os","values":["Windows","Linux"]},)"
    R"({"name":"browser","values":["Chrome","Firefox"]}],"strength":2)";

std::string MixedCaseParamsFile() {
  const std::string path = TempPath("case_params.json");
  WriteFile(path, std::string("{") + kMixedCaseParameters + "}");
  return path;
}

/// @brief A model whose seeds, constraint and weights are spelled as declared.
std::string DeclaredSpellingModel() {
  return std::string("{") + kMixedCaseParameters +
         R"(,"seed":3,"seeds":[{"os":"Windows","browser":"Chrome"}],)"
         R"("constraints":["IF os = Linux THEN browser != Firefox"],)"
         R"("weights":{"browser":{"Firefox":9}}})";
}

/// @brief The same model with every caller-written name in a different case.
std::string OtherCaseModel() {
  return std::string("{") + kMixedCaseParameters +
         R"(,"seed":3,"seeds":[{"os":"wInDoWs","browser":"cHROME"}],)"
         R"("constraints":["IF os = LINUX THEN browser != firefox"],)"
         R"("weights":{"browser":{"fIREFOx":9}}})";
}

}  // namespace

// A suite generated before a model's values were re-cased, or written by hand
// against a runner that spells them differently, is the ordinary thing to feed
// back in. Every name the caller writes -- a seed value, a constraint operand,
// a weights key -- resolves by ASCII case folding, and to the same value the
// declared spelling names: two models differing only in the case of those names
// produce the same suite, byte for byte.
TEST(CliValueResolutionTest, SeedsConstraintsAndWeightsResolveInAnyAsciiCase) {
  const std::string declared_path = TempPath("case_declared_model.json");
  WriteFile(declared_path, DeclaredSpellingModel());
  const std::string other_path = TempPath("case_other_model.json");
  WriteFile(other_path, OtherCaseModel());

  const auto declared = RunCliCaptureStderr("generate " + declared_path);
  ASSERT_EQ(declared.exit_code, 0) << declared.stdout_text;
  const auto other = RunCliCaptureStderr("generate " + other_path);
  ASSERT_EQ(other.exit_code, 0) << other.stdout_text;

  EXPECT_EQ(other.stdout_text, declared.stdout_text);
  EXPECT_NE(declared.stdout_text.find(R"("warnings":[])"), std::string::npos)
      << declared.stdout_text;
}

// The weight has to reach the value, not merely be accepted alongside it: a
// weights key the gate resolved but the engine dropped would leave the caller
// with a silently unweighted run. The unweighted suite is the control.
TEST(CliValueResolutionTest, AWeightKeyInAnotherCaseStillWeightsItsValue) {
  const std::string base = std::string("{") + kMixedCaseParameters + R"(,"seed":7)";

  const std::string unweighted_path = TempPath("case_weight_none.json");
  WriteFile(unweighted_path, base + "}");
  const std::string declared_path = TempPath("case_weight_declared.json");
  WriteFile(declared_path, base + R"(,"weights":{"browser":{"Firefox":50}}})");
  const std::string other_path = TempPath("case_weight_other.json");
  WriteFile(other_path, base + R"(,"weights":{"browser":{"fIREFOx":50}}})");

  const auto unweighted = RunCli("generate " + unweighted_path);
  ASSERT_EQ(unweighted.exit_code, 0) << unweighted.stdout_text;
  const auto declared = RunCli("generate " + declared_path);
  ASSERT_EQ(declared.exit_code, 0) << declared.stdout_text;
  const auto other = RunCli("generate " + other_path);
  ASSERT_EQ(other.exit_code, 0) << other.stdout_text;

  EXPECT_NE(declared.stdout_text, unweighted.stdout_text)
      << "the weight has to change the suite for this case to measure anything";
  EXPECT_EQ(other.stdout_text, declared.stdout_text);
}

// An analyzed row spelled in another case is a row about this model, so it is
// credited for what it covers and is not one of the rows the report calls
// invalid.
TEST(CliValueResolutionTest, AnAnalyzedRowInAnotherCaseIsCreditedNotRejected) {
  const std::string params_path = MixedCaseParamsFile();

  const std::string declared_path = TempPath("case_analyze_declared.json");
  WriteFile(declared_path, R"([{"os":"Windows","browser":"Chrome"},)"
                           R"({"os":"Linux","browser":"Firefox"}])");
  const std::string other_path = TempPath("case_analyze_other.json");
  WriteFile(other_path, R"([{"os":"wINDOWS","browser":"chrome"},)"
                        R"({"os":"LINUX","browser":"FireFOX"}])");

  const auto declared = RunCli("analyze --params " + params_path + " --tests " + declared_path);
  const auto other = RunCli("analyze --params " + params_path + " --tests " + other_path);

  // Two of four pairs are covered either way, so the run ends on the documented
  // insufficient-coverage code rather than on invalid input.
  EXPECT_EQ(other.exit_code, declared.exit_code);
  EXPECT_EQ(other.exit_code, 2) << other.stdout_text;
  EXPECT_EQ(other.stdout_text, declared.stdout_text);
  EXPECT_NE(other.stdout_text.find(R"("invalidTests":[])"), std::string::npos) << other.stdout_text;
}

// An existing suite spelled in another case keeps its coverage credit: extend
// tops it up rather than regenerating the rows it already holds.
TEST(CliValueResolutionTest, AnExistingRowInAnotherCaseKeepsItsCoverageCredit) {
  const std::string params_path = MixedCaseParamsFile();

  const std::string declared_path = TempPath("case_extend_declared.json");
  WriteFile(declared_path, R"([{"os":"Windows","browser":"Chrome"},)"
                           R"({"os":"Linux","browser":"Firefox"}])");
  const std::string other_path = TempPath("case_extend_other.json");
  WriteFile(other_path, R"([{"os":"wINDOWS","browser":"chrome"},)"
                        R"({"os":"LINUX","browser":"FireFOX"}])");

  const auto declared = RunCli("extend --existing " + declared_path + " " + params_path);
  ASSERT_EQ(declared.exit_code, 0) << declared.stdout_text;
  const auto other = RunCli("extend --existing " + other_path + " " + params_path);
  ASSERT_EQ(other.exit_code, 0) << other.stdout_text;

  // The rows are handed back in the spelling they were supplied in, so the two
  // reports differ there and nowhere else: the same statistics, the same
  // generated tail, and no warning about a row left out of the figure.
  const std::string stats = R"("stats":{"totalTuples":4,"coveredTuples":4,"testCount":4})";
  EXPECT_NE(declared.stdout_text.find(stats), std::string::npos) << declared.stdout_text;
  EXPECT_NE(other.stdout_text.find(stats), std::string::npos) << other.stdout_text;
  EXPECT_NE(other.stdout_text.find(R"("warnings":[])"), std::string::npos) << other.stdout_text;
  EXPECT_NE(other.stdout_text.find(R"({"os":"Linux","browser":"Chrome"})"), std::string::npos)
      << other.stdout_text;
}

// Two weights keys naming one value carry two weights for it, and only one can
// apply. A key spelled the way the model declares the value settles that
// outright; with no declared spelling among them the winner would come down to
// the order the caller's map is walked in, which is not the same on every
// surface, so the model is refused on the documented invalid-input code.
TEST(CliValueResolutionTest, TwoWeightKeysNamingOneValueAreRefused) {
  const std::string base = std::string("{") + kMixedCaseParameters;

  const std::string ambiguous_path = TempPath("case_weight_ambiguous.json");
  WriteFile(ambiguous_path, base + R"(,"weights":{"os":{"wINdows":5,"WINDOWS":9}}})");
  const auto refused = RunCliStderrOnly("generate " + ambiguous_path);
  EXPECT_EQ(refused.exit_code, 3);
  EXPECT_EQ(refused.stdout_text,
            "error: Ambiguous value in weights: os=WINDOWS and os=wINdows name the same value\n");

  // The declared spelling settles it, so this stays a model the caller can run.
  const std::string settled_path = TempPath("case_weight_settled.json");
  WriteFile(settled_path, base + R"(,"weights":{"os":{"Windows":5,"wINdows":9}}})");
  const auto accepted = RunCli("generate " + settled_path);
  EXPECT_EQ(accepted.exit_code, 0) << accepted.stdout_text;
}

// The fold is ASCII, and widening it would make two names a model is entitled
// to keep apart resolve to one value. A name differing only in the case of a
// non-ASCII letter stays unknown on both exit paths: refused outright where the
// row has to be a test case for this model, recorded as invalid where it does
// not.
TEST(CliValueResolutionTest, ANonAsciiCaseDifferenceIsStillAnUnknownValue) {
  const std::string params = R"("parameters":[{"name":"city","values":["MÜNCHEN","OSAKA"]},)"
                             R"({"name":"n","values":["1","2"]}])";

  const std::string model_path = TempPath("case_nonascii_model.json");
  WriteFile(model_path, "{" + params + R"(,"seeds":[{"city":"MüNCHEN","n":"1"}]})");
  const auto seeded = RunCliStderrOnly("generate " + model_path);
  EXPECT_EQ(seeded.exit_code, 3);
  EXPECT_EQ(seeded.stdout_text, "error: seeds 0 parameter 'city' has unknown value 'MüNCHEN'\n");

  const std::string params_path = TempPath("case_nonascii_params.json");
  WriteFile(params_path, "{" + params + "}");
  const std::string rows_path = TempPath("case_nonascii_rows.json");
  WriteFile(rows_path, R"([{"city":"MüNCHEN","n":"1"}])");
  const auto analyzed = RunCli("analyze --params " + params_path + " --tests " + rows_path);
  EXPECT_EQ(analyzed.exit_code, 3) << analyzed.stdout_text;
  EXPECT_NE(
      analyzed.stdout_text.find(R"("reason":"value 'MüNCHEN' is not declared by parameter city")"),
      std::string::npos)
      << analyzed.stdout_text;

  // The ASCII half of the same value still folds, so this is the fold's reach
  // and not an absence of folding.
  const std::string ascii_rows_path = TempPath("case_nonascii_ascii_rows.json");
  WriteFile(ascii_rows_path, R"([{"city":"osaka","n":"1"}])");
  const auto ascii = RunCli("analyze --params " + params_path + " --tests " + ascii_rows_path);
  EXPECT_EQ(ascii.exit_code, 2) << ascii.stdout_text;
  EXPECT_NE(ascii.stdout_text.find(R"("invalidTests":[])"), std::string::npos) << ascii.stdout_text;
}

// ---------------------------------------------------------------------------
// Delivering output.
// ---------------------------------------------------------------------------

namespace {

/// @brief Run the CLI with its standard output closed, keeping stderr.
///
/// `2>&1` copies the capture pipe onto standard error before `>&-` closes
/// standard output, so the diagnostic is still read back while every write to
/// the report stream fails.
CliResult RunCliWithClosedStdout(const std::string& args) {
  return RunCommandLine(std::string(COVERWISE_CLI_PATH) + " " + args + " 2>&1 >&-");
}

}  // namespace

// Standard output fails late: a closed pipe or a filesystem with no room left
// surfaces when the stream is flushed, after the last insertion has returned.
// A command that ends without looking exits 0 beside a truncated report, and a
// caller gating on the exit code reads that document as a complete result.
TEST(CliOutputTest, AReportThatCouldNotBeWrittenIsNotReportedAsSuccess) {
  const std::string model_path = TempPath("output_stream_model.json");
  WriteFile(model_path, R"({"parameters":[{"name":"a","values":["x","y"]},)"
                        R"({"name":"b","values":["1","2"]}]})");
  const std::string rows_path = TempPath("output_stream_rows.json");
  WriteFile(rows_path,
            R"([{"a":"x","b":"1"},{"a":"x","b":"2"},{"a":"y","b":"1"},{"a":"y","b":"2"}])");

  const std::vector<std::string> commands = {
      "generate " + model_path,
      "stats " + model_path,
      "analyze --params " + model_path + " --tests " + rows_path,
      "extend --existing " + rows_path + " " + model_path,
  };

  for (const auto& args : commands) {
    // Each of these writes a complete report and succeeds when the stream is
    // there to take it, so the exit code below is about the stream alone.
    const auto delivered = RunCli(args);
    EXPECT_EQ(delivered.exit_code, 0) << args << ": " << delivered.stdout_text;

    const auto undelivered = RunCliWithClosedStdout(args);
    EXPECT_EQ(undelivered.exit_code, 3) << args << ": " << undelivered.stdout_text;
    EXPECT_NE(undelivered.stdout_text.find("cannot write to standard output"), std::string::npos)
        << args << ": " << undelivered.stdout_text;
  }
}

namespace {

/// @brief Report size this section treats as larger than any pipe will hold.
///
/// A reader that stops early only fails the writes the kernel could not absorb
/// on its own, so the report has to outrun the pipe for the run to be about the
/// stream at all. Mainstream kernels hand out at most 64 KiB; the models below
/// are sized several times past that, and the test measures rather than trusts.
constexpr size_t kBeyondPipeBuffer = 256 * 1024;

/// @brief Shape of the model whose rendered rows outrun a pipe.
///
/// Width, value count and name length all multiply into every row, so a model
/// this modest reaches hundreds of kilobytes without slowing the run down.
constexpr int kLongReportParameters = 30;
constexpr int kLongReportValues = 8;

std::string LongReportName(int index) { return std::string(70, 'p') + std::to_string(index); }

std::string LongReportValue(int index) { return std::string(70, 'v') + std::to_string(index); }

/// @brief A model whose generated suite is far longer than a pipe will hold.
std::string LongReportModel() {
  std::ostringstream model;
  model << R"({"parameters":[)";
  for (int p = 0; p < kLongReportParameters; ++p) {
    if (p > 0) model << ',';
    model << R"({"name":")" << LongReportName(p) << R"(","values":[)";
    for (int v = 0; v < kLongReportValues; ++v) {
      if (v > 0) model << ',';
      model << '"' << LongReportValue(v) << '"';
    }
    model << "]}";
  }
  model << "]}";
  return model.str();
}

/// @brief One row of LongReportModel, as a suite for analyze and extend.
///
/// A single row leaves nearly every tuple uncovered, which is what makes the
/// coverage report as long as the generated suite it is measured against.
std::string LongReportRow() {
  std::ostringstream row;
  row << R"([{)";
  for (int p = 0; p < kLongReportParameters; ++p) {
    if (p > 0) row << ',';
    row << '"' << LongReportName(p) << R"(":")" << LongReportValue(0) << '"';
  }
  row << "}]";
  return row.str();
}

/// @brief A model whose statistics alone outrun a pipe.
///
/// `stats` prints one entry per parameter and nothing per test case, so the
/// parameter list is the only thing that can carry it past the buffer.
std::string WideStatsModel() {
  const std::string padding(250, 'q');
  std::ostringstream model;
  model << R"({"parameters":[)";
  for (int p = 0; p < 1000; ++p) {
    if (p > 0) model << ',';
    model << R"({"name":")" << padding << p << R"(","values":["a","b"]})";
  }
  model << "]}";
  return model.str();
}

/// @brief Outcome of a run whose reader stopped before the report ended.
struct EarlyCloseResult {
  int exit_code = -1;
  std::string stderr_text;
};

/// @brief Run the CLI into a reader that takes one byte and closes the pipe.
///
/// The shell's own status belongs to the reader, so the CLI's status is
/// recorded from inside the pipeline and read back from a file. A run that
/// ended on a signal rather than a return arrives here as 128 plus the signal
/// number, which is outside the documented set and fails the assertion.
EarlyCloseResult RunCliIntoReaderClosingEarly(const std::string& args) {
  const std::string status_path = TempPath("early_close_status.txt");
  const std::string stderr_path = TempPath("early_close_stderr.txt");
  RunCommandLine("{ " + std::string(COVERWISE_CLI_PATH) + " " + args + " 2>" + stderr_path +
                 "; echo $? >" + status_path + "; } | head -c 1 >/dev/null");

  EarlyCloseResult result;
  std::ifstream status(status_path);
  status >> result.exit_code;
  std::ifstream diagnostics(stderr_path);
  std::ostringstream text;
  text << diagnostics.rdbuf();
  result.stderr_text = text.str();
  return result;
}

}  // namespace

// A reader that stops early — `coverwise generate model.json | head` — closes
// the pipe while the report is still being written. That failed write is the
// only way the run learns its report was cut short, and the caller has to be
// able to tell it from a run that delivered everything, so it ends on a code
// from the table usage prints with a diagnostic beside it, never on a signal.
TEST(CliOutputTest, AReaderThatClosesEarlyEndsTheRunOnADocumentedCode) {
  const std::string model_path = TempPath("early_close_model.json");
  WriteFile(model_path, LongReportModel());
  const std::string rows_path = TempPath("early_close_rows.json");
  WriteFile(rows_path, LongReportRow());
  const std::string wide_path = TempPath("early_close_wide_model.json");
  WriteFile(wide_path, WideStatsModel());

  const std::vector<std::string> commands = {
      "generate " + model_path,
      "stats " + wide_path,
      "analyze --params " + model_path + " --tests " + rows_path,
      "extend --existing " + rows_path + " " + model_path,
  };

  for (const auto& args : commands) {
    // The premise of the run below: this report is longer than a pipe will
    // absorb, so writes do outlive the reader.
    const auto delivered = RunCli(args);
    EXPECT_GT(delivered.stdout_text.size(), kBeyondPipeBuffer) << args;

    const auto truncated = RunCliIntoReaderClosingEarly(args);
    EXPECT_EQ(truncated.exit_code, 3) << args << ": " << truncated.stderr_text;
    EXPECT_NE(truncated.stderr_text.find("cannot write to standard output"), std::string::npos)
        << args << ": " << truncated.stderr_text;
  }
}

// Usage is short enough for a pipe to hold all of it, so whether the write
// outlives the reader is a matter of which side finishes first. Either outcome
// is acceptable; what a caller may never see is a status outside the very table
// usage prints, which is what an unhandled signal would produce.
TEST(CliHelpTest, UsageEndsOnADocumentedCodeWhateverBecomesOfTheStream) {
  const auto truncated = RunCliIntoReaderClosingEarly("--help");
  EXPECT_GE(truncated.exit_code, 0) << truncated.stderr_text;
  EXPECT_LE(truncated.exit_code, 3) << truncated.stderr_text;

  // With no stream at all the outcome is not a race: the usage write fails, and
  // --help accounts for it the way a report-writing subcommand does.
  const auto closed = RunCliWithClosedStdout("--help");
  EXPECT_EQ(closed.exit_code, 3) << closed.stdout_text;
  EXPECT_NE(closed.stdout_text.find("cannot write to standard output"), std::string::npos)
      << closed.stdout_text;
}

// Usage the caller asked for is output the command produced, so it goes to
// standard output where a redirect or a pipe can read it. Usage printed because
// an invocation was wrong is a diagnostic and stays on standard error.
TEST(CliHelpTest, RequestedUsageGoesToStandardOutputAndDiagnosedUsageToStandardError) {
  for (const char* flag : {"--help", "-h"}) {
    const auto requested = RunCli(flag);
    EXPECT_EQ(requested.exit_code, 0) << flag;
    EXPECT_NE(requested.stdout_text.find("coverwise generate"), std::string::npos) << flag;
    EXPECT_NE(requested.stdout_text.find("Exit codes:"), std::string::npos) << flag;
    EXPECT_EQ(RunCliStderrOnly(flag).stdout_text, "") << flag;
  }

  const auto no_command = RunCli("");
  EXPECT_EQ(no_command.exit_code, 3) << no_command.stdout_text;
  EXPECT_EQ(no_command.stdout_text, "");
  EXPECT_NE(RunCliStderrOnly("").stdout_text.find("coverwise generate"), std::string::npos);

  const auto unknown_command = RunCli("nosuchcommand");
  EXPECT_EQ(unknown_command.exit_code, 3) << unknown_command.stdout_text;
  EXPECT_EQ(unknown_command.stdout_text, "");
  EXPECT_NE(RunCliStderrOnly("nosuchcommand").stdout_text.find("Unknown command"),
            std::string::npos);
}
