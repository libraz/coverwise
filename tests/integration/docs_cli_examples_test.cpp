/// @file docs_cli_examples_test.cpp
/// @brief Holds the CLI reference's worked examples to the shipped binary.
///
/// Each command section of `docs/*/cli.md` carries one invocation in a `bash`
/// block followed by JSON blocks: every block but the last is an input file, in
/// the order the invocation names its placeholders, and the last is the stdout
/// that invocation produces. The examples are therefore executable, and this
/// test runs them rather than trusting that they were transcribed correctly.
///
/// Comparison ignores whitespace outside string literals, because documentation
/// wraps the output for readability while the CLI writes it as a single line.
/// Everything else -- key order, numeric form, every value -- has to match.

#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifndef COVERWISE_CLI_PATH
#error "COVERWISE_CLI_PATH must be defined by the build"
#endif
#ifndef COVERWISE_REPO_ROOT
#error "COVERWISE_REPO_ROOT must be defined by the build"
#endif

namespace {

/// @brief A fenced code block, with the info string that opened it.
struct DocBlock {
  std::string info;
  std::string body;
};

/// @brief One command section of the CLI reference.
struct DocSection {
  std::string command;
  std::vector<DocBlock> blocks;
};

std::string ReadRepoFile(const std::string& relative) {
  const std::string path = std::string(COVERWISE_REPO_ROOT) + "/" + relative;
  std::ifstream stream(path, std::ios::binary);
  EXPECT_TRUE(stream.is_open()) << "Cannot open " << path;
  std::ostringstream buffer;
  buffer << stream.rdbuf();
  return buffer.str();
}

std::vector<std::string> SplitLines(const std::string& text) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  return lines;
}

std::string Trim(const std::string& value) {
  size_t begin = 0;
  size_t end = value.size();
  while (begin < end && std::isspace(static_cast<unsigned char>(value[begin]))) ++begin;
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) --end;
  return value.substr(begin, end - begin);
}

/// @brief The token inside the first pair of backticks of a line.
std::string BacktickedToken(const std::string& line) {
  const size_t open = line.find('`');
  if (open == std::string::npos) return "";
  const size_t close = line.find('`', open + 1);
  if (close == std::string::npos) return "";
  return line.substr(open + 1, close - open - 1);
}

/// @brief Split a document into the sections its headings introduce.
///
/// A section is named by the backticked token of its heading, which is how the
/// reference names a command, so both translations are read the same way.
std::vector<DocSection> DocumentSections(const std::string& relative) {
  std::vector<DocSection> sections;
  DocSection current;
  DocBlock block;
  bool in_block = false;

  for (const auto& line : SplitLines(ReadRepoFile(relative))) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("```", 0) == 0) {
      if (in_block) {
        current.blocks.push_back(std::move(block));
        block = DocBlock{};
        in_block = false;
      } else {
        block.info = Trim(trimmed.substr(3));
        in_block = true;
      }
      continue;
    }
    if (in_block) {
      block.body += line;
      block.body += '\n';
      continue;
    }
    if (trimmed.rfind("#", 0) == 0) {
      if (!current.command.empty()) sections.push_back(std::move(current));
      current = DocSection{};
      current.command = BacktickedToken(trimmed);
    }
  }
  if (!current.command.empty()) sections.push_back(std::move(current));
  return sections;
}

std::vector<const DocBlock*> BlocksWithInfo(const DocSection& section, const std::string& info) {
  std::vector<const DocBlock*> found;
  for (const auto& block : section.blocks) {
    if (block.info == info) found.push_back(&block);
  }
  return found;
}

/// @brief Strip whitespace that lies outside a JSON string literal.
std::string CompactJson(const std::string& text) {
  std::string compact;
  compact.reserve(text.size());
  bool in_string = false;
  bool escaped = false;
  for (char c : text) {
    if (in_string) {
      compact.push_back(c);
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    if (c == '"') in_string = true;
    compact.push_back(c);
  }
  return compact;
}

/// @brief Drop the bracketed optional groups of a documented invocation.
std::string WithoutOptionalArguments(const std::string& invocation) {
  std::string required;
  int depth = 0;
  for (char c : invocation) {
    if (c == '[') {
      ++depth;
      continue;
    }
    if (c == ']') {
      if (depth > 0) --depth;
      continue;
    }
    if (depth == 0) required.push_back(c);
  }
  return required;
}

/// @brief The `<placeholder>` spans of an invocation, in the order they appear.
std::vector<std::string> Placeholders(const std::string& invocation) {
  std::vector<std::string> found;
  size_t i = 0;
  while (i < invocation.size()) {
    const size_t open = invocation.find('<', i);
    if (open == std::string::npos) break;
    const size_t close = invocation.find('>', open + 1);
    if (close == std::string::npos) break;
    found.push_back(invocation.substr(open, close - open + 1));
    i = close + 1;
  }
  return found;
}

void WriteFile(const std::string& path, const std::string& content) {
  std::ofstream out(path);
  ASSERT_TRUE(out.is_open()) << "cannot open " << path;
  out << content;
}

std::string TempPath(const std::string& suffix) {
  static int counter = 0;
  std::ostringstream ss;
  ss << "/tmp/coverwise_docs_example_" << getpid() << "_" << counter++ << "_" << suffix;
  return ss.str();
}

/// @brief Run a shell command, returning its stdout.
std::string RunCapturingStdout(const std::string& command) {
  FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");
  EXPECT_NE(pipe, nullptr) << "popen failed for: " << command;
  std::string output;
  if (!pipe) return output;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) output += buffer;
  pclose(pipe);
  return output;
}

/// @brief Run the invocation a section documents against the inputs it shows.
/// @return The CLI's stdout, or an empty string when the section is malformed.
std::string RunDocumentedExample(const std::string& document, const DocSection& section) {
  const auto bash_blocks = BlocksWithInfo(section, "bash");
  const auto json_blocks = BlocksWithInfo(section, "json");
  EXPECT_FALSE(bash_blocks.empty())
      << document << " section `" << section.command << "` shows JSON but no invocation";
  if (bash_blocks.empty()) return "";

  std::string invocation;
  for (const auto& line : SplitLines(bash_blocks.front()->body)) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("coverwise", 0) == 0) invocation = trimmed;
  }
  EXPECT_FALSE(invocation.empty())
      << document << " section `" << section.command << "` shows no coverwise invocation";
  if (invocation.empty()) return "";

  invocation = WithoutOptionalArguments(invocation);
  const auto placeholders = Placeholders(invocation);
  const size_t input_count = json_blocks.size() - 1;
  EXPECT_EQ(placeholders.size(), input_count)
      << document << " section `" << section.command << "` shows " << input_count
      << " input blocks for " << placeholders.size() << " required arguments";
  if (placeholders.size() != input_count) return "";

  for (size_t i = 0; i < placeholders.size(); ++i) {
    const std::string path = TempPath(section.command + "_" + std::to_string(i) + ".json");
    WriteFile(path, json_blocks[i]->body);
    const size_t at = invocation.find(placeholders[i]);
    invocation.replace(at, placeholders[i].size(), path);
  }

  // The invocation names the installed command; run the binary under test.
  const std::string command = "'" + std::string(COVERWISE_CLI_PATH) + "'" +
                              invocation.substr(std::string("coverwise").size());
  return RunCapturingStdout(command);
}

/// @brief Count the objects in the array that follows @p key in compact JSON.
///
/// Every array this test inspects holds objects, so an element is exactly a
/// brace opened at the array's own nesting level.
/// @return -1 when the key is absent or its value is not an array.
int CountArrayObjects(const std::string& compact, const std::string& key) {
  const std::string marker = "\"" + key + "\":[";
  const size_t at = compact.find(marker);
  if (at == std::string::npos) return -1;

  int depth = 0;
  int elements = 0;
  bool in_string = false;
  bool escaped = false;
  for (size_t i = at + marker.size(); i < compact.size(); ++i) {
    const char c = compact[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
    } else if (c == '[') {
      ++depth;
    } else if (c == '{') {
      if (depth == 0) ++elements;
      ++depth;
    } else if (c == '}') {
      --depth;
    } else if (c == ']') {
      if (depth == 0) break;
      --depth;
    }
  }
  return elements;
}

/// @brief Read the integer value of @p key from compact JSON.
/// @return -1 when the key is absent.
long ReadNumber(const std::string& compact, const std::string& key) {
  const std::string marker = "\"" + key + "\":";
  const size_t at = compact.find(marker);
  if (at == std::string::npos) return -1;
  return std::strtol(compact.c_str() + at + marker.size(), nullptr, 10);
}

const char* const kCliDocuments[] = {
    "docs/en/cli.md",
    "docs/ja/cli.md",
};

/// @brief The command sections whose examples must stay executable.
const char* const kDocumentedCommands[] = {"generate", "analyze", "stats"};

/// @brief The sections of a document that show an invocation and its output.
std::vector<DocSection> ExecutableSections(const std::string& document) {
  std::vector<DocSection> executable;
  for (auto& section : DocumentSections(document)) {
    if (BlocksWithInfo(section, "json").size() < 2) continue;
    if (BlocksWithInfo(section, "bash").empty()) continue;
    executable.push_back(std::move(section));
  }
  return executable;
}

}  // namespace

TEST(DocsCliExamplesTest, EveryDocumentedCommandShowsAnExecutableExample) {
  for (const char* document : kCliDocuments) {
    const auto sections = ExecutableSections(document);
    for (const char* command : kDocumentedCommands) {
      bool found = false;
      for (const auto& section : sections) {
        if (section.command == command) found = true;
      }
      EXPECT_TRUE(found) << document << " no longer shows an input and an output for `" << command
                         << "`";
    }
  }
}

TEST(DocsCliExamplesTest, DocumentedOutputMatchesTheCli) {
  for (const char* document : kCliDocuments) {
    for (const auto& section : ExecutableSections(document)) {
      const auto json_blocks = BlocksWithInfo(section, "json");
      const std::string documented = CompactJson(json_blocks.back()->body);
      const std::string actual = CompactJson(RunDocumentedExample(document, section));
      EXPECT_EQ(actual, documented) << document << " section `" << section.command
                                    << "` does not show what the CLI writes for the input it shows";
    }
  }
}

TEST(DocsCliExamplesTest, CliDocumentsShowTheSameExamplesInEveryLanguage) {
  std::vector<std::string> reference;
  for (const char* document : kCliDocuments) {
    std::vector<std::string> blocks;
    for (const auto& section : ExecutableSections(document)) {
      for (const auto& block : section.blocks) {
        if (block.info == "json") blocks.push_back(section.command + " " + CompactJson(block.body));
      }
    }
    if (reference.empty()) {
      reference = std::move(blocks);
      continue;
    }
    EXPECT_EQ(blocks, reference) << document << " diverges from " << kCliDocuments[0];
  }
}

/// @brief Assert that a coverage report lists every tuple it did not omit.
///
/// @p origin names where the report came from, so a failure says whether the
/// documentation or the implementation broke the invariant.
void ExpectUncoveredArrayIsComplete(const std::string& report, const std::string& origin) {
  const long uncovered_count = ReadNumber(report, "uncoveredCount");
  if (uncovered_count < 0) return;

  const long omitted = ReadNumber(report, "omittedUncovered");
  ASSERT_GE(omitted, 0) << origin << " reports uncoveredCount without omittedUncovered";
  const int listed = CountArrayObjects(report, "uncovered");
  ASSERT_GE(listed, 0) << origin << " reports uncoveredCount without an uncovered array";
  EXPECT_EQ(static_cast<long>(listed), uncovered_count - omitted)
      << origin << ": the uncovered array does not hold every tuple that was not omitted";
}

TEST(DocsCliExamplesTest, TheCoverageReportAccountsForEveryUncoveredTuple) {
  for (const char* document : kCliDocuments) {
    for (const auto& section : ExecutableSections(document)) {
      const std::string origin = std::string(document) + " section `" + section.command + "`";
      ExpectUncoveredArrayIsComplete(CompactJson(BlocksWithInfo(section, "json").back()->body),
                                     origin);
      ExpectUncoveredArrayIsComplete(CompactJson(RunDocumentedExample(document, section)),
                                     "The CLI, driven by " + origin);
    }
  }
}
