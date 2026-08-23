#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "model/limits.h"
#include "model/surface_error.h"

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

/// @brief Run the CLI with the given argument string, capturing stdout and exit code.
///
/// The exit code is recovered from popen's pclose status (WEXITSTATUS).
CliResult RunCli(const std::string& args) {
  std::string command = std::string(COVERWISE_CLI_PATH) + " " + args + " 2>/dev/null";
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

/// @brief Run the CLI capturing merged stdout+stderr and the exit code.
///
/// Unlike RunCli, this keeps stderr so diagnostics can be asserted on.
CliResult RunCliCaptureStderr(const std::string& args) {
  std::string command = std::string(COVERWISE_CLI_PATH) + " " + args + " 2>&1";
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
// file and from standard input alike.
TEST(CliInputBudgetTest, OversizedSingleStringIsRejectedFromEitherSource) {
  std::string oversized = R"({"parameters":[{"name":"a","values":[")";
  oversized.append(coverwise::model::kMaxStringBytes + 1, 'x');
  oversized += R"("]}]})";
  const std::string path = TempPath("oversized_string.json");
  WriteFile(path, oversized);

  const auto from_file = RunCliCaptureStderr("generate " + path);
  EXPECT_EQ(from_file.exit_code, 3) << from_file.stdout_text;
  EXPECT_NE(from_file.stdout_text.find("string exceeds"), std::string::npos)
      << from_file.stdout_text;

  const auto from_stdin = RunCliCaptureStderr("generate - < " + path);
  EXPECT_EQ(from_stdin.exit_code, 3) << from_stdin.stdout_text;
  EXPECT_NE(from_stdin.stdout_text.find("string exceeds"), std::string::npos)
      << from_stdin.stdout_text;
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
  EXPECT_NE(result.stdout_text.find("Input strings exceed"), std::string::npos)
      << result.stdout_text;
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

TEST(SurfaceErrorTest, AnAbsentDetailLeavesNoSeparatorBehind) {
  const Error without_detail{Error::Code::kInvalidInput, "At least one parameter is required", ""};
  EXPECT_EQ(SurfaceError(without_detail).text(), "At least one parameter is required");

  const Error with_detail{Error::Code::kInvalidInput, "Invalid strength", "strength must be >= 1"};
  EXPECT_EQ(SurfaceError(with_detail).text(), "Invalid strength: strength must be >= 1");
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
