#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

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

}  // namespace

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
  EXPECT_NE(result.stdout_text.find("\"coverageRatio\":1.000"), std::string::npos)
      << result.stdout_text;
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
  EXPECT_NE(result.stdout_text.find("\"coverageRatio\":1.000"), std::string::npos)
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
