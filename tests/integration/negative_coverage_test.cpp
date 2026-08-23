#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
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
  ss << "/tmp/coverwise_negative_coverage_test_" << getpid() << "_" << counter++ << "_" << suffix;
  return ss.str();
}

/// @brief Run the CLI with the given argument string, capturing stdout and exit code.
CliResult RunCli(const std::string& args) {
  const std::string command = std::string(COVERWISE_CLI_PATH) + " " + args + " 2>/dev/null";
  FILE* pipe = popen(command.c_str(), "r");
  EXPECT_NE(pipe, nullptr) << "popen failed for: " << command;
  CliResult result;
  if (!pipe) return result;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    result.stdout_text += buffer;
  }
  const int status = pclose(pipe);
  result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

/// @brief Extract the body of the `"negativeCoverage"` object, braces excluded.
std::optional<std::string> NegativeCoverageObject(const std::string& json) {
  const std::string key = "\"negativeCoverage\"";
  const size_t key_pos = json.find(key);
  if (key_pos == std::string::npos) return std::nullopt;
  const size_t open = json.find('{', key_pos);
  if (open == std::string::npos) return std::nullopt;
  const size_t close = json.find('}', open);
  if (close == std::string::npos) return std::nullopt;
  return json.substr(open + 1, close - open - 1);
}

/// @brief Read one numeric member out of a flat JSON object body.
std::optional<double> NumberField(const std::string& object_body, const std::string& name) {
  const std::string key = "\"" + name + "\"";
  const size_t key_pos = object_body.find(key);
  if (key_pos == std::string::npos) return std::nullopt;
  const size_t colon = object_body.find(':', key_pos);
  if (colon == std::string::npos) return std::nullopt;
  const char* begin = object_body.c_str() + colon + 1;
  char* end = nullptr;
  const double value = std::strtod(begin, &end);
  if (end == begin) return std::nullopt;
  return value;
}

/// @brief Model whose only invalid value drives negative generation to completion.
constexpr const char* kCompleteModel =
    R"({"parameters":[)"
    R"({"name":"A","values":["a0",{"value":"bad","invalid":true}]},)"
    R"({"name":"B","values":["b0","b1","b2"]},)"
    R"({"name":"C","values":["c0","c1","c2"]},)"
    R"({"name":"D","values":["d0","d1","d2"]}],)"
    R"("strength":4,"seed":42})";

/// @brief The same shape of model, capped so negative generation stops early.
constexpr const char* kTruncatedModel =
    R"({"parameters":[)"
    R"({"name":"A","values":["a0",{"value":"bad","invalid":true}]},)"
    R"({"name":"B","values":["b0","b1"]}],)"
    R"("strength":2,"maxTests":3,"seed":42})";

}  // namespace

// `negativeCoverage` is documented as part of the CLI's generate output, and its
// three counts have to describe one universe: every negative tuple is either
// covered by a generated negative test or omitted.
TEST(CliNegativeCoverageTest, CountsPartitionTheNegativeTupleUniverse) {
  const std::string path = TempPath("complete.json");
  WriteFile(path, kCompleteModel);

  const auto result = RunCli("generate " + path);
  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;

  const auto object = NegativeCoverageObject(result.stdout_text);
  ASSERT_TRUE(object.has_value()) << result.stdout_text;

  const auto total = NumberField(*object, "totalTuples");
  const auto covered = NumberField(*object, "coveredTuples");
  const auto omitted = NumberField(*object, "omittedTuples");
  const auto ratio = NumberField(*object, "coverageRatio");
  ASSERT_TRUE(total.has_value() && covered.has_value() && omitted.has_value() && ratio.has_value())
      << *object;

  EXPECT_GT(*total, 0.0) << *object;
  EXPECT_DOUBLE_EQ(*covered + *omitted, *total) << *object;
  EXPECT_DOUBLE_EQ(*omitted, 0.0) << *object;
  EXPECT_DOUBLE_EQ(*ratio, 1.0) << *object;
}

// The maxTests cap stops negative generation part way. The omitted count must
// then account for the shortfall instead of staying at its default of zero,
// which would report a truncated suite as complete negative coverage.
TEST(CliNegativeCoverageTest, TruncatedSuiteReportsTheOmittedTuples) {
  const std::string path = TempPath("truncated.json");
  WriteFile(path, kTruncatedModel);

  const auto result = RunCli("generate " + path);
  ASSERT_EQ(result.exit_code, 0) << result.stdout_text;

  const auto object = NegativeCoverageObject(result.stdout_text);
  ASSERT_TRUE(object.has_value()) << result.stdout_text;

  const auto total = NumberField(*object, "totalTuples");
  const auto covered = NumberField(*object, "coveredTuples");
  const auto omitted = NumberField(*object, "omittedTuples");
  const auto ratio = NumberField(*object, "coverageRatio");
  ASSERT_TRUE(total.has_value() && covered.has_value() && omitted.has_value() && ratio.has_value())
      << *object;

  EXPECT_GT(*omitted, 0.0) << *object;
  EXPECT_DOUBLE_EQ(*covered + *omitted, *total) << *object;
  EXPECT_DOUBLE_EQ(*ratio, *covered / *total) << *object;
}
