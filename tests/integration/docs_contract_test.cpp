/// @file docs_contract_test.cpp
/// @brief Holds the shipped documentation to the implementation it describes.
///
/// Every claim checked here was hand-synchronised at least once and drifted
/// again, so each one is derived mechanically: constraint samples are parsed,
/// operator and builder inventories are extracted from the sources that define
/// them, and the published benchmark counts are regenerated.

#include <gtest/gtest.h>

#include <cctype>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/constraint_parser.h"
#include "model/parameter.h"

using coverwise::core::Generate;
using coverwise::model::GenerateOptions;
using coverwise::model::Parameter;
using coverwise::model::ParseConstraint;

namespace {

/// UTF-8 encoding of U+00D7, the separator used in the benchmark tables.
constexpr const char* kMultiplicationSign = "\xC3\x97";

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

// --- Markdown extraction ---

/// @brief Split a markdown table row into its trimmed cells.
/// @return Empty when the line is not a table row.
std::vector<std::string> TableCells(const std::string& line) {
  const std::string trimmed = Trim(line);
  if (trimmed.size() < 2 || trimmed.front() != '|') return {};
  std::vector<std::string> cells;
  size_t begin = 1;
  while (begin <= trimmed.size()) {
    const size_t pipe = trimmed.find('|', begin);
    if (pipe == std::string::npos) break;
    cells.push_back(Trim(trimmed.substr(begin, pipe - begin)));
    begin = pipe + 1;
  }
  return cells;
}

/// @brief Collect the token inside the first pair of backticks of a cell.
std::string BacktickedToken(const std::string& cell) {
  const size_t open = cell.find('`');
  if (open == std::string::npos) return "";
  const size_t close = cell.find('`', open + 1);
  if (close == std::string::npos) return "";
  return cell.substr(open + 1, close - open - 1);
}

/// @brief Collect the backticked token in the first column of every table row.
///
/// Reference tables are located by the shape of what they list rather than by
/// their heading, so the same extraction works for every translation of a
/// document and a new language needs no change here.
std::vector<std::string> FirstColumnTokens(const std::vector<std::string>& lines) {
  std::vector<std::string> tokens;
  for (const auto& line : lines) {
    const auto cells = TableCells(line);
    if (cells.empty()) continue;
    const std::string token = BacktickedToken(cells[0]);
    if (!token.empty()) tokens.push_back(token);
  }
  return tokens;
}

bool IsUppercaseWord(const std::string& token) {
  if (token.size() < 2) return false;
  for (char c : token) {
    if (c < 'A' || c > 'Z') return false;
  }
  return true;
}

/// @brief The keywords a constraint document lists in its keyword table.
std::set<std::string> DocumentedKeywords(const std::vector<std::string>& lines) {
  std::set<std::string> keywords;
  for (const auto& token : FirstColumnTokens(lines)) {
    if (IsUppercaseWord(token)) keywords.insert(token);
  }
  return keywords;
}

/// @brief The wildcards a constraint document lists in its wildcard table.
std::set<std::string> DocumentedWildcards(const std::vector<std::string>& lines) {
  std::set<std::string> wildcards;
  for (const auto& token : FirstColumnTokens(lines)) {
    if (token.size() == 1 && !std::isalnum(static_cast<unsigned char>(token[0]))) {
      wildcards.insert(token);
    }
  }
  return wildcards;
}

/// @brief The builder methods a constraint document lists, by name.
std::set<std::string> DocumentedBuilderMethods(const std::vector<std::string>& lines) {
  std::set<std::string> methods;
  for (const auto& token : FirstColumnTokens(lines)) {
    const size_t paren = token.find('(');
    if (paren != std::string::npos && paren > 0) methods.insert(token.substr(0, paren));
  }
  return methods;
}

/// @brief Collect the constraint expressions a constraint document shows.
///
/// Two shapes carry them: a fenced block with no info string, whose every line
/// is one expression, and a quoted array element inside a TypeScript block.
std::vector<std::string> DocumentedConstraintExpressions(const std::vector<std::string>& lines) {
  std::vector<std::string> expressions;
  bool in_block = false;
  bool plain_block = false;
  bool typescript_block = false;

  for (const auto& line : lines) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("```", 0) == 0) {
      if (in_block) {
        in_block = false;
        plain_block = false;
        typescript_block = false;
      } else {
        const std::string info = Trim(trimmed.substr(3));
        in_block = true;
        plain_block = info.empty();
        typescript_block = (info == "typescript");
      }
      continue;
    }
    if (!in_block || trimmed.empty()) continue;

    if (plain_block) {
      expressions.push_back(trimmed);
      continue;
    }
    if (typescript_block && trimmed.front() == '\'') {
      const size_t close = trimmed.find('\'', 1);
      if (close != std::string::npos) {
        expressions.push_back(trimmed.substr(1, close - 1));
      }
    }
  }
  return expressions;
}

// --- Source extraction ---

/// @brief Read the braced body that starts on the line holding @p opener.
std::vector<std::string> BracedBody(const std::vector<std::string>& lines,
                                    const std::string& opener) {
  std::vector<std::string> body;
  int depth = 0;
  bool started = false;
  for (const auto& line : lines) {
    if (!started && line.find(opener) == std::string::npos) continue;
    if (!started) {
      started = true;
      for (char c : line) {
        if (c == '{') ++depth;
        if (c == '}') --depth;
      }
      continue;
    }
    for (char c : line) {
      if (c == '{') ++depth;
      if (c == '}') --depth;
    }
    if (depth <= 0) break;
    body.push_back(line);
  }
  return body;
}

/// @brief Collect every string literal compared in the parser's keyword table.
std::set<std::string> ParserKeywords() {
  const auto lines = SplitLines(ReadRepoFile("src/model/constraint_parser.cpp"));
  const std::string marker = "upper == \"";
  std::set<std::string> keywords;
  for (const auto& line : BracedBody(lines, "TokenType ClassifyKeyword(")) {
    const size_t at = line.find(marker);
    if (at == std::string::npos) continue;
    const size_t begin = at + marker.size();
    const size_t end = line.find('"', begin);
    if (end != std::string::npos) keywords.insert(line.substr(begin, end - begin));
  }
  return keywords;
}

/// @brief Collect the pattern characters the glob matcher gives special meaning.
std::set<std::string> GlobMatcherWildcards() {
  const auto lines = SplitLines(ReadRepoFile("src/model/constraint_ast.cpp"));
  const std::string marker = "pattern_codepoints[pi] == static_cast<uint32_t>('";
  std::set<std::string> wildcards;
  for (const auto& line : BracedBody(lines, "bool LikeNode::GlobMatch(")) {
    const size_t at = line.find(marker);
    if (at == std::string::npos) continue;
    wildcards.insert(line.substr(at + marker.size(), 1));
  }
  return wildcards;
}

/// @brief Collect every method declared by the builder's exported interfaces.
std::set<std::string> BuilderInterfaceMethods() {
  const auto lines = SplitLines(ReadRepoFile("js/constraint.ts"));
  std::set<std::string> methods;
  bool inside = false;
  for (const auto& line : lines) {
    if (!inside) {
      inside = line.rfind("export interface ", 0) == 0 && !line.empty() && line.back() == '{';
      continue;
    }
    const std::string trimmed = Trim(line);
    if (trimmed == "}") {
      inside = false;
      continue;
    }
    size_t i = 0;
    while (i < trimmed.size() &&
           (std::isalnum(static_cast<unsigned char>(trimmed[i])) || trimmed[i] == '_')) {
      ++i;
    }
    if (i > 0 && i < trimmed.size() && trimmed[i] == '(') methods.insert(trimmed.substr(0, i));
  }
  return methods;
}

// --- Fixtures ---

/// @brief The vocabulary every constraint sample in the documentation uses.
std::vector<Parameter> DocumentationModel() {
  return {
      {"os", {"Windows", "macOS", "Linux", "iOS", "Android"}},
      {"browser", {"Safari", "Chrome", "Chromium", "Edge", "IE", "Firefox"}},
      {"filesystem", {"NTFS", "FAT32", "APFS", "HFS+", "ext4", "btrfs", "xfs"}},
      {"language", {"C++", "c99", "rust"}},
      {"build_system", {"make (BSD)", "ninja"}},
      {"release", {"1.0 (beta)", "1.0"}},
      {"channel", {"preview", "stable"}},
      {"arch", {"x64", "arm64", "arm32"}},
      {"device", {"phone", "desktop"}},
      {"age", {"16", "18", "21"}},
      {"plan", {"child", "adult"}},
      {"price", {"0", "100"}},
      {"status", {"error", "ok"}},
      {"count", {"10", "100", "200"}},
      {"mode", {"batch", "copy", "compatibility", "interactive"}},
      {"priority", {"1", "3", "5"}},
      {"queue", {"high", "low"}},
      {"engine", {"blink", "gecko"}},
      {"version", {"1.0.0", "1.2.3"}},
      {"is_major", {"true", "false"}},
      {"code", {"v1.0", "v2.0"}},
      {"generation", {"first", "second"}},
      {"source", {"a", "b"}},
      {"target", {"a", "b"}},
      {"input_format", {"json", "yaml"}},
      {"output_format", {"json", "yaml"}},
      {"convert", {"true", "false"}},
      {"screen_size", {"5", "7", "10"}},
  };
}

std::vector<Parameter> MakeUniformParams(uint32_t count, uint32_t values_per_param) {
  std::vector<Parameter> params;
  params.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    std::vector<std::string> values;
    values.reserve(values_per_param);
    for (uint32_t j = 0; j < values_per_param; ++j) {
      values.push_back("v" + std::to_string(j));
    }
    params.emplace_back("P" + std::to_string(i), std::move(values));
  }
  return params;
}

// --- Benchmark table extraction ---

/// @brief One published benchmark configuration.
struct BenchmarkKey {
  uint32_t parameters;
  uint32_t values;
  uint32_t strength;

  bool operator<(const BenchmarkKey& other) const {
    if (parameters != other.parameters) return parameters < other.parameters;
    if (values != other.values) return values < other.values;
    return strength < other.strength;
  }
};

/// @brief The counts a performance document publishes for one configuration.
struct BenchmarkClaim {
  uint32_t tuples;
  uint32_t tests;
};

/// @brief Read an integer that may carry thousands separators.
bool ParseCount(const std::string& cell, uint32_t* out) {
  std::string digits;
  for (char c : cell) {
    if (c == ',') continue;
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    digits.push_back(c);
  }
  if (digits.empty()) return false;
  *out = static_cast<uint32_t>(std::stoul(digits));
  return true;
}

/// @brief Collect the leading integers of a configuration cell, which names a
///        parameter count and a value count before any descriptive suffix.
std::vector<uint32_t> LeadingIntegers(const std::string& cell) {
  std::vector<uint32_t> numbers;
  size_t i = 0;
  while (i < cell.size() && numbers.size() < 2) {
    if (!std::isdigit(static_cast<unsigned char>(cell[i]))) {
      ++i;
      continue;
    }
    size_t end = i;
    while (end < cell.size() && std::isdigit(static_cast<unsigned char>(cell[end]))) ++end;
    numbers.push_back(static_cast<uint32_t>(std::stoul(cell.substr(i, end - i))));
    i = end;
  }
  return numbers;
}

/// @brief Read every benchmark row a performance document publishes.
///
/// Rows are recognised by shape rather than by heading, so both languages are
/// read the same way: the configuration cell holds the two dimensions joined by
/// a multiplication sign, and a `t-wise` cell in second position names a
/// strength other than pairwise.
std::map<BenchmarkKey, BenchmarkClaim> PublishedBenchmarks(const std::string& relative) {
  std::map<BenchmarkKey, BenchmarkClaim> rows;
  for (const auto& line : SplitLines(ReadRepoFile(relative))) {
    const auto cells = TableCells(line);
    if (cells.size() < 4) continue;
    if (cells[0].find(kMultiplicationSign) == std::string::npos) continue;
    const auto dimensions = LeadingIntegers(cells[0]);
    if (dimensions.size() != 2) continue;

    uint32_t strength = 2;
    size_t tuples_cell = 1;
    const size_t wise = cells[1].find("-wise");
    if (wise != std::string::npos) {
      if (!ParseCount(cells[1].substr(0, wise), &strength)) continue;
      tuples_cell = 2;
    }

    BenchmarkClaim claim{};
    if (!ParseCount(cells[tuples_cell], &claim.tuples)) continue;
    if (!ParseCount(cells[tuples_cell + 1], &claim.tests)) continue;

    const BenchmarkKey key{dimensions[0], dimensions[1], strength};
    EXPECT_EQ(rows.count(key), 0u) << relative << " lists the same configuration twice";
    rows[key] = claim;
  }
  return rows;
}

const char* const kPerformanceDocuments[] = {
    "README.md",
    "README_ja.md",
    "docs/en/introduction.md",
    "docs/ja/introduction.md",
};

const char* const kConstraintDocuments[] = {
    "docs/en/constraints.md",
    "docs/ja/constraints.md",
};

const char* const kValueFormatDocuments[] = {
    "docs/en/cli.md",
    "docs/ja/cli.md",
    "docs/en/js-api.md",
    "docs/ja/js-api.md",
};

/// @brief True when a line spells out an alias list rather than declaring one,
///        which is what separates the sample from the type it illustrates.
bool ListsAliasLiterals(const std::string& line) {
  const size_t at = line.find("aliases");
  if (at == std::string::npos) return false;
  return line.find_first_of("\"'", at) != std::string::npos;
}

/// @brief Collect the quoted names of the sample that shows the value formats.
///
/// The block is the one that spells out an alias list, in either JSON or
/// TypeScript form. Keys are skipped -- a JSON key is followed by a colon and a
/// TypeScript key is not quoted -- so what remains is exactly the parameter
/// name, the values, their aliases and their class names: the set the validator
/// has to resolve unambiguously.
std::vector<std::string> ValueFormatSampleNames(const std::vector<std::string>& lines) {
  std::vector<std::string> block;
  std::vector<std::string> current;
  bool in_block = false;
  for (const auto& line : lines) {
    if (Trim(line).rfind("```", 0) == 0) {
      if (in_block) {
        bool lists_aliases = false;
        for (const auto& body : current) {
          if (ListsAliasLiterals(body)) lists_aliases = true;
        }
        if (lists_aliases && block.empty()) block = current;
        current.clear();
      }
      in_block = !in_block;
      continue;
    }
    if (in_block) current.push_back(line);
  }

  std::vector<std::string> names;
  for (const auto& line : block) {
    size_t i = 0;
    while (i < line.size()) {
      if (line[i] != '"' && line[i] != '\'') {
        ++i;
        continue;
      }
      const char quote = line[i];
      const size_t close = line.find(quote, i + 1);
      if (close == std::string::npos) break;
      size_t after = close + 1;
      while (after < line.size() && std::isspace(static_cast<unsigned char>(line[after]))) ++after;
      const bool is_key = after < line.size() && line[after] == ':';
      if (!is_key) names.push_back(line.substr(i + 1, close - i - 1));
      i = close + 1;
    }
  }
  return names;
}

std::string FoldAscii(const std::string& value) {
  std::string folded = value;
  for (auto& c : folded) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return folded;
}

}  // namespace

// --- Constraint samples ---

TEST(DocsContractTest, EveryDocumentedConstraintExpressionParses) {
  const auto params = DocumentationModel();
  for (const char* document : kConstraintDocuments) {
    const auto expressions = DocumentedConstraintExpressions(SplitLines(ReadRepoFile(document)));
    ASSERT_FALSE(expressions.empty()) << document << " shows no constraint expression";
    for (const auto& expression : expressions) {
      auto result = ParseConstraint(expression, params);
      EXPECT_TRUE(result.error.ok())
          << document << " shows an expression the parser rejects: " << expression << " -- "
          << result.error.message;
      EXPECT_NE(result.constraint, nullptr) << document << ": " << expression;
    }
  }
}

TEST(DocsContractTest, ConstraintDocumentsShowTheSameExpressionsInEveryLanguage) {
  std::vector<std::string> reference;
  for (const char* document : kConstraintDocuments) {
    auto expressions = DocumentedConstraintExpressions(SplitLines(ReadRepoFile(document)));
    if (reference.empty()) {
      reference = std::move(expressions);
      continue;
    }
    EXPECT_EQ(expressions, reference) << document << " diverges from " << kConstraintDocuments[0];
  }
}

// --- Parameter validation contract ---

TEST(DocsContractTest, TheDocumentedValueFormatSampleIsAccepted) {
  for (const char* document : kValueFormatDocuments) {
    const auto names = ValueFormatSampleNames(SplitLines(ReadRepoFile(document)));
    ASSERT_FALSE(names.empty()) << document << " shows no value-format sample";

    std::set<std::string> folded;
    for (const auto& name : names) {
      EXPECT_TRUE(folded.insert(FoldAscii(name)).second)
          << document << " shows a sample whose names collide once ASCII case is folded: " << name;
    }
  }
}

TEST(DocsContractTest, CaseFoldedCollisionsAreRejectedAsDocumented) {
  Parameter browser("browser", {"Chrome", "IE", "Chromium", "Firefox"});
  browser.set_aliases({{}, {}, {"chromium-browser", "cr"}, {}});
  EXPECT_TRUE(coverwise::model::ValidateParameters({browser}).ok());

  Parameter case_only_values("browser", {"Chrome", "chrome"});
  auto value_error = coverwise::model::ValidateParameters({case_only_values});
  EXPECT_EQ(value_error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_NE(value_error.message.find("Ambiguous value or alias"), std::string::npos)
      << value_error.message;

  Parameter aliased("browser", {"Chrome", "Chromium"});
  aliased.set_aliases({{}, {"chrome"}});
  auto alias_error = coverwise::model::ValidateParameters({aliased});
  EXPECT_EQ(alias_error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_NE(alias_error.message.find("Ambiguous value or alias"), std::string::npos)
      << alias_error.message;

  auto name_error = coverwise::model::ValidateParameters(
      {Parameter("os", {"Windows"}), Parameter("OS", {"Linux"})});
  EXPECT_EQ(name_error.code, coverwise::model::Error::Code::kInvalidInput);
  EXPECT_NE(name_error.message.find("differ only by ASCII case"), std::string::npos)
      << name_error.message;
}

// --- Operator, wildcard and builder inventories ---

TEST(DocsContractTest, DocumentedKeywordsMatchTheParserKeywordTable) {
  const auto keywords = ParserKeywords();
  ASSERT_FALSE(keywords.empty()) << "Could not read the parser keyword table";
  for (const char* document : kConstraintDocuments) {
    EXPECT_EQ(DocumentedKeywords(SplitLines(ReadRepoFile(document))), keywords)
        << document << " does not list exactly the keywords the parser accepts";
  }
}

TEST(DocsContractTest, DocumentedWildcardsMatchTheGlobMatcher) {
  const auto wildcards = GlobMatcherWildcards();
  ASSERT_FALSE(wildcards.empty()) << "Could not read the glob matcher wildcards";
  for (const char* document : kConstraintDocuments) {
    EXPECT_EQ(DocumentedWildcards(SplitLines(ReadRepoFile(document))), wildcards)
        << document << " does not list exactly the wildcards LIKE honors";
  }
}

TEST(DocsContractTest, DocumentedBuilderMethodsMatchTheBuilderInterface) {
  const auto methods = BuilderInterfaceMethods();
  ASSERT_FALSE(methods.empty()) << "Could not read the builder interfaces";
  for (const char* document : kConstraintDocuments) {
    EXPECT_EQ(DocumentedBuilderMethods(SplitLines(ReadRepoFile(document))), methods)
        << document << " does not list exactly the builder's public methods";
  }
}

// --- Published benchmark numbers ---

TEST(DocsContractTest, PerformanceDocumentsPublishTheSameBenchmarkTable) {
  const auto reference = PublishedBenchmarks(kPerformanceDocuments[0]);
  ASSERT_FALSE(reference.empty()) << kPerformanceDocuments[0] << " publishes no benchmark row";

  for (const char* document : kPerformanceDocuments) {
    const auto rows = PublishedBenchmarks(document);
    ASSERT_EQ(rows.size(), reference.size()) << document << " publishes a different row count";
    for (const auto& [key, claim] : reference) {
      const auto found = rows.find(key);
      ASSERT_NE(found, rows.end()) << document << " omits " << key.parameters << " parameters of "
                                   << key.values << " values at strength " << key.strength;
      EXPECT_EQ(found->second.tuples, claim.tuples)
          << document << " disagrees on the tuple count of " << key.parameters << " parameters of "
          << key.values << " values at strength " << key.strength;
      EXPECT_EQ(found->second.tests, claim.tests)
          << document << " disagrees on the test count of " << key.parameters << " parameters of "
          << key.values << " values at strength " << key.strength;
    }
  }
}

TEST(DocsContractTest, PublishedBenchmarkCountsMatchGeneratedSuites) {
  for (const auto& [key, claim] : PublishedBenchmarks(kPerformanceDocuments[0])) {
    GenerateOptions opts;
    opts.parameters = MakeUniformParams(key.parameters, key.values);
    opts.strength = key.strength;
    opts.seed = 42;

    auto result = Generate(opts);
    EXPECT_EQ(result.stats.total_tuples, claim.tuples)
        << "Published tuple count is stale for " << key.parameters << " parameters of "
        << key.values << " values at strength " << key.strength;
    EXPECT_EQ(result.tests.size(), claim.tests)
        << "Published test count is stale for " << key.parameters << " parameters of " << key.values
        << " values at strength " << key.strength;
  }
}
