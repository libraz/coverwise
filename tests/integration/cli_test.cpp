#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
