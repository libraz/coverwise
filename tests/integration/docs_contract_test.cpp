/// @file docs_contract_test.cpp
/// @brief Holds the shipped documentation to the implementation it describes.
///
/// The target set is the documentation tree itself: every markdown file under
/// `docs/` plus the product READMEs beside it, found by walking the repository
/// rather than by naming files. Each claim is recognised by the shape of what a
/// document contains, not by which document contains it, so a page written
/// tomorrow is read by these checks the day it lands.
///
/// A document is passed over only when `kDocumentsWithoutCheckableClaims`
/// records why it carries nothing this gate can check, and that record is itself
/// checked: an entry that turns out to carry a claim fails, so the list cannot
/// be used to silence a document.

#include <gtest/gtest.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/constraint_parser.h"
#include "model/parameter.h"

using coverwise::core::EstimateModel;
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

// --- The documents under this gate ---

/// Every shipped document falls into exactly one of three categories: checked
/// here, checked by a named test elsewhere, or carrying no claim this gate can
/// measure. The two lists below hold the second and third; everything the walk
/// finds and neither list names is checked here.

/// @brief A document this gate reads past, and the reason it holds no claim.
struct UncheckedDocument {
  const char* path;
  const char* reason;
};

/// @brief A document another test tier checks, the test that checks it, and why
///        the check belongs there rather than here.
struct DelegatedDocument {
  const char* path;
  const char* checker;
  const char* reason;
};

const UncheckedDocument kDocumentsWithoutCheckableClaims[] = {
    {"CHANGELOG.md",
     "Records what each released version did. Its statements are history: they "
     "describe a version that already shipped rather than asserting anything "
     "about the code as it stands now, which is the only thing a document can "
     "be measured against here. It restates no limit value and no acceptance "
     "rule."},
    {"SECURITY.md",
     "States how to report a vulnerability and which versions are supported. "
     "Nothing in it is derived from the engine."},
    {"docs/en/glossary.md",
     "Defines the vocabulary the other documents use. It shows no sample, "
     "inventory or measurement taken from the implementation, so every term it "
     "defines is checked where the API that uses the term is documented."},
    {"docs/ja/glossary.md",
     "The translation of the vocabulary page, and claim-free for the same "
     "reason."},
};

const DelegatedDocument kDocumentsCheckedElsewhere[] = {
    {"npm-readme.md", "js/packaging.test.ts",
     "The npm package page. The claim worth gating on it is the CDN snippet, "
     "and checking it means fetching the URL the page names, which this tier "
     "cannot do. Reading the page here as well would put one document under two "
     "sets of rules, which is the drift this gate exists to prevent."},
};

/// @brief Every document this gate is responsible for.
///
/// Both the repository root and the documentation tree are enumerated rather
/// than matched against a name pattern. A document added under `docs/` is
/// therefore gated without anything here being edited, and renaming one at the
/// root cannot drop it out of coverage the way a `README*.md` pattern would --
/// it stays enumerated under its new name, or its entry below stops resolving.
/// Either way the change is announced rather than silent.
// What the repository ships is what it tracks, so the build derives this list
// from the git index rather than from a walk of the working copy: an untracked
// file -- an agent instruction file, a local draft -- is not a document the
// project publishes, and a gate that read one would reach a different verdict
// on a developer's machine than on a clean checkout.
std::vector<std::string> ShippedDocuments() {
  std::vector<std::string> documents = {COVERWISE_SHIPPED_DOCUMENTS};
  std::sort(documents.begin(), documents.end());
  return documents;
}

bool IsUnchecked(const std::string& document) {
  for (const auto& entry : kDocumentsWithoutCheckableClaims) {
    if (document == entry.path) return true;
  }
  return false;
}

bool IsCheckedElsewhere(const std::string& document) {
  for (const auto& entry : kDocumentsCheckedElsewhere) {
    if (document == entry.path) return true;
  }
  return false;
}

std::vector<std::string> GatedDocuments() {
  std::vector<std::string> gated;
  for (auto& document : ShippedDocuments()) {
    if (IsUnchecked(document) || IsCheckedElsewhere(document)) continue;
    gated.push_back(std::move(document));
  }
  return gated;
}

/// @brief The translations of one document, keyed by the directory naming the
///        language, for every document that lives under `docs/<language>/`.
std::map<std::string, std::map<std::string, std::string>> TranslationGroups() {
  std::map<std::string, std::map<std::string, std::string>> groups;
  for (const auto& document : ShippedDocuments()) {
    const size_t first = document.find('/');
    if (first == std::string::npos || document.substr(0, first) != "docs") continue;
    const size_t second = document.find('/', first + 1);
    if (second == std::string::npos) continue;
    const std::string language = document.substr(first + 1, second - first - 1);
    groups[document.substr(second + 1)][language] = document;
  }
  return groups;
}

/// @brief Every language the documentation tree is written in.
std::set<std::string> DocumentLanguages() {
  std::set<std::string> languages;
  for (const auto& group : TranslationGroups()) {
    for (const auto& translation : group.second) languages.insert(translation.first);
  }
  return languages;
}

// --- Markdown extraction ---

/// @brief A fenced code block, with the info string that opened it.
struct FencedBlock {
  std::string info;
  std::vector<std::string> lines;
};

std::vector<FencedBlock> FencedBlocks(const std::vector<std::string>& lines) {
  std::vector<FencedBlock> blocks;
  FencedBlock current;
  bool inside = false;
  for (const auto& line : lines) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("```", 0) == 0) {
      if (inside) {
        blocks.push_back(std::move(current));
        current = FencedBlock{};
        inside = false;
      } else {
        current.info = Trim(trimmed.substr(3));
        inside = true;
      }
      continue;
    }
    if (inside) current.lines.push_back(line);
  }
  return blocks;
}

std::string JoinLines(const std::vector<std::string>& lines) {
  std::string joined;
  for (const auto& line : lines) {
    joined += line;
    joined += '\n';
  }
  return joined;
}

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

/// @brief The keywords a document lists in a keyword table.
std::set<std::string> DocumentedKeywords(const std::vector<std::string>& lines) {
  std::set<std::string> keywords;
  for (const auto& token : FirstColumnTokens(lines)) {
    if (IsUppercaseWord(token)) keywords.insert(token);
  }
  return keywords;
}

/// @brief The wildcards a document lists in a wildcard table.
std::set<std::string> DocumentedWildcards(const std::vector<std::string>& lines) {
  std::set<std::string> wildcards;
  for (const auto& token : FirstColumnTokens(lines)) {
    if (token.size() == 1 && !std::isalnum(static_cast<unsigned char>(token[0]))) {
      wildcards.insert(token);
    }
  }
  return wildcards;
}

/// @brief The builder methods a document lists, by name.
///
/// A builder method is called on whatever the previous call returned, so the
/// reference spells it unqualified. A dotted name in the same position belongs
/// to a different table -- an API summary naming its receiver -- and is left to
/// the checks that read that table.
std::set<std::string> DocumentedBuilderMethods(const std::vector<std::string>& lines) {
  std::set<std::string> methods;
  for (const auto& token : FirstColumnTokens(lines)) {
    const size_t paren = token.find('(');
    if (paren == std::string::npos || paren == 0) continue;
    const std::string name = token.substr(0, paren);
    if (name.find('.') != std::string::npos) continue;
    methods.insert(name);
  }
  return methods;
}

/// @brief A quoted literal of a source line, with what surrounds it.
struct QuotedLiteral {
  std::string text;
  size_t begin = 0;      ///< Offset of the opening quote.
  bool is_key = false;   ///< A colon follows the closing quote.
  char preceded_by = 0;  ///< Last non-space character before the opening quote.
};

/// @brief Collect every quoted literal of @p text, in order.
std::vector<QuotedLiteral> QuotedLiterals(const std::string& text) {
  std::vector<QuotedLiteral> literals;
  size_t i = 0;
  char previous = 0;
  while (i < text.size()) {
    const char c = text[i];
    if (c != '"' && c != '\'') {
      if (!std::isspace(static_cast<unsigned char>(c))) previous = c;
      ++i;
      continue;
    }
    size_t end = i + 1;
    while (end < text.size() && text[end] != c) {
      if (text[end] == '\\') ++end;
      ++end;
    }
    if (end >= text.size()) break;

    QuotedLiteral literal;
    literal.text = text.substr(i + 1, end - i - 1);
    literal.begin = i;
    literal.preceded_by = previous;
    size_t after = end + 1;
    while (after < text.size() && std::isspace(static_cast<unsigned char>(text[after]))) ++after;
    literal.is_key = after < text.size() && text[after] == ':';
    literals.push_back(std::move(literal));

    previous = c;
    i = end + 1;
  }
  return literals;
}

/// @brief A constraint expression a document shows, and how it shows it.
struct ConstraintClaim {
  std::string expression;
  bool shown_as_rejected = false;
};

/// @brief True when a block shows its input being refused rather than accepted.
bool ShowsRejection(const FencedBlock& block) {
  for (const auto& line : block.lines) {
    if (line.find("except ") != std::string::npos) return true;
    if (line.find("catch (") != std::string::npos) return true;
  }
  return false;
}

/// @brief Collect the string elements of the `constraints` arrays of @p body.
///
/// An element is a quoted literal the array itself holds: opened by the array or
/// introduced by one of its commas, and nested inside nothing. A literal that
/// sits inside a call is an argument of the constraint builder, which composes an
/// expression rather than spelling one out, so it is not one of these.
std::vector<std::string> ConstraintArrayElements(const std::string& body) {
  std::vector<std::string> elements;
  const std::string marker = "constraints";
  size_t search = 0;
  while (true) {
    const size_t at = body.find(marker, search);
    if (at == std::string::npos) break;
    search = at + marker.size();

    size_t open = search;
    while (open < body.size() && body[open] != '[') {
      if (!std::isspace(static_cast<unsigned char>(body[open])) && body[open] != ':' &&
          body[open] != '=' && body[open] != '"' && body[open] != '\'') {
        break;
      }
      ++open;
    }
    if (open >= body.size() || body[open] != '[') continue;

    int depth = 0;
    char previous = 0;
    size_t i = open;
    for (; i < body.size(); ++i) {
      const char c = body[i];
      if (c == '"' || c == '\'') {
        size_t end = i + 1;
        while (end < body.size() && body[end] != c) {
          if (body[end] == '\\') ++end;
          ++end;
        }
        if (end >= body.size()) break;
        if (depth == 1 && (previous == '[' || previous == ',')) {
          elements.push_back(body.substr(i + 1, end - i - 1));
        }
        previous = c;
        i = end;
        continue;
      }
      if (c == '[' || c == '(' || c == '{') ++depth;
      if (c == ']' || c == ')' || c == '}') --depth;
      if (!std::isspace(static_cast<unsigned char>(c))) previous = c;
      if (depth == 0) break;
    }
    search = i;
  }
  return elements;
}

/// @brief Collect the constraint expressions a document shows.
///
/// Two shapes carry them: a fenced block with no info string, whose every line
/// is one expression, and a string element of a `constraints` array in a block
/// of any language.
std::vector<ConstraintClaim> DocumentedConstraintExpressions(
    const std::vector<FencedBlock>& blocks) {
  std::vector<ConstraintClaim> claims;
  for (const auto& block : blocks) {
    const bool rejected = ShowsRejection(block);
    if (block.info.empty()) {
      for (const auto& line : block.lines) {
        const std::string trimmed = Trim(line);
        if (!trimmed.empty()) claims.push_back({trimmed, rejected});
      }
      continue;
    }
    for (auto& element : ConstraintArrayElements(JoinLines(block.lines))) {
      claims.push_back({std::move(element), rejected});
    }
  }
  return claims;
}

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
std::vector<std::string> ValueFormatSampleNames(const std::vector<FencedBlock>& blocks) {
  const std::vector<std::string>* sample = nullptr;
  for (const auto& block : blocks) {
    for (const auto& line : block.lines) {
      if (!ListsAliasLiterals(line)) continue;
      sample = &block.lines;
      break;
    }
    if (sample != nullptr) break;
  }
  if (sample == nullptr) return {};

  std::vector<std::string> names;
  for (const auto& line : *sample) {
    for (const auto& literal : QuotedLiterals(line)) {
      if (!literal.is_key) names.push_back(literal.text);
    }
  }
  return names;
}

/// @brief A block that declares a model and then draws what generating it
///        prints, one test case per comment line.
struct SuiteListing {
  std::vector<Parameter> parameters;
  std::vector<std::string> constraints;
  uint32_t strength = 2;
  uint64_t seed = 0;
  std::vector<std::vector<std::string>> rows;
};

/// @brief The value a numeric field is declared with in @p body.
/// @return @p fallback when the field is absent.
uint64_t DeclaredNumber(const std::string& body, const std::string& field, uint64_t fallback) {
  size_t search = 0;
  while (true) {
    const size_t at = body.find(field, search);
    if (at == std::string::npos) return fallback;
    search = at + field.size();
    const bool starts_word =
        at == 0 || (!std::isalnum(static_cast<unsigned char>(body[at - 1])) && body[at - 1] != '_');
    const bool ends_word =
        search >= body.size() ||
        (!std::isalnum(static_cast<unsigned char>(body[search])) && body[search] != '_');
    if (!starts_word || !ends_word) continue;

    size_t value = search;
    while (value < body.size() && !std::isdigit(static_cast<unsigned char>(body[value]))) {
      if (body[value] == '\n') break;
      ++value;
    }
    if (value < body.size() && std::isdigit(static_cast<unsigned char>(body[value]))) {
      return std::stoull(body.substr(value));
    }
  }
}

/// @brief The comment marker a line opens with, if any.
/// @return The text after the marker, or an empty optional-like flag.
bool CommentBody(const std::string& line, std::string* body) {
  const std::string trimmed = Trim(line);
  if (trimmed.rfind("//", 0) == 0) {
    *body = Trim(trimmed.substr(2));
    return true;
  }
  if (trimmed.rfind("#", 0) == 0) {
    *body = Trim(trimmed.substr(1));
    return true;
  }
  return false;
}

/// @brief Read the model and the printed test cases a listing block shows.
///
/// A parameter is declared by a line that names it and then opens a list of its
/// values, which both the object form and the mapping form of the samples
/// satisfy. A printed test case is a comment line holding a record, whose
/// non-key literals are its values in the order the parameters were declared.
/// @return False when the block draws no test case.
bool ReadSuiteListing(const FencedBlock& block, SuiteListing* listing) {
  std::vector<std::string> rows_text;
  for (const auto& line : block.lines) {
    std::string comment;
    if (CommentBody(line, &comment) && !comment.empty() && comment.front() == '{') {
      rows_text.push_back(comment);
      continue;
    }
    if (!rows_text.empty()) continue;

    const size_t bracket = line.find('[');
    if (bracket == std::string::npos) continue;
    const auto literals = QuotedLiterals(line);
    if (literals.size() < 2 || literals.front().begin > bracket) continue;

    std::vector<std::string> values;
    for (size_t i = 1; i < literals.size(); ++i) values.push_back(literals[i].text);
    listing->parameters.emplace_back(literals.front().text, std::move(values));
  }
  if (rows_text.empty() || listing->parameters.empty()) return false;

  const std::string body = JoinLines(block.lines);
  listing->strength = static_cast<uint32_t>(DeclaredNumber(body, "strength", 2));
  listing->seed = DeclaredNumber(body, "seed", 0);
  listing->constraints = ConstraintArrayElements(body);
  for (const auto& row : rows_text) {
    std::vector<std::string> values;
    for (const auto& literal : QuotedLiterals(row)) {
      if (!literal.is_key) values.push_back(literal.text);
    }
    listing->rows.push_back(std::move(values));
  }
  return true;
}

std::vector<SuiteListing> DocumentedSuiteListings(const std::vector<FencedBlock>& blocks) {
  std::vector<SuiteListing> listings;
  for (const auto& block : blocks) {
    SuiteListing listing;
    if (ReadSuiteListing(block, &listing)) listings.push_back(std::move(listing));
  }
  return listings;
}

/// @brief Reduce a C++ program to what a compiler sees: comments dropped, and
///        whitespace outside literals collapsed to what separates two words.
std::string NormalizeProgram(const std::string& source) {
  std::string stripped;
  stripped.reserve(source.size());
  for (size_t i = 0; i < source.size();) {
    const char c = source[i];
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '/') {
      while (i < source.size() && source[i] != '\n') ++i;
      continue;
    }
    if (c == '/' && i + 1 < source.size() && source[i + 1] == '*') {
      i += 2;
      while (i + 1 < source.size() && !(source[i] == '*' && source[i + 1] == '/')) ++i;
      i = std::min(i + 2, source.size());
      continue;
    }
    if (c == '"' || c == '\'') {
      size_t end = i + 1;
      while (end < source.size() && source[end] != c) {
        if (source[end] == '\\') ++end;
        ++end;
      }
      end = std::min(end + 1, source.size());
      stripped.append(source, i, end - i);
      i = end;
      continue;
    }
    stripped.push_back(c);
    ++i;
  }

  const auto is_word = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
  };
  std::string normalized;
  normalized.reserve(stripped.size());
  for (size_t i = 0; i < stripped.size();) {
    if (stripped[i] == '"' || stripped[i] == '\'') {
      const char quote = stripped[i];
      size_t end = i + 1;
      while (end < stripped.size() && stripped[end] != quote) {
        if (stripped[end] == '\\') ++end;
        ++end;
      }
      end = std::min(end + 1, stripped.size());
      normalized.append(stripped, i, end - i);
      i = end;
      continue;
    }
    if (!std::isspace(static_cast<unsigned char>(stripped[i]))) {
      normalized.push_back(stripped[i]);
      ++i;
      continue;
    }
    size_t end = i;
    while (end < stripped.size() && std::isspace(static_cast<unsigned char>(stripped[end]))) ++end;
    const bool joins_words = !normalized.empty() && end < stripped.size() &&
                             is_word(normalized.back()) && is_word(stripped[end]);
    if (joins_words) normalized.push_back(' ');
    i = end;
  }
  return normalized;
}

/// @brief The C++ programs a CMake fixture compiles against the installed
///        package on behalf of the documentation, keyed by fixture name.
///
/// Fixture directories are found on disk, so adding one puts its program under
/// this gate without a list being touched.
std::map<std::string, std::string> DocumentationFixturePrograms() {
  namespace fs = std::filesystem;
  const fs::path fixtures = fs::path(COVERWISE_REPO_ROOT) / "tests" / "cmake";
  std::map<std::string, std::string> programs;
  std::error_code ec;
  for (fs::directory_iterator it(fixtures, ec), end; it != end; it.increment(ec)) {
    if (ec) break;
    if (!it->is_directory()) continue;
    const std::string name = it->path().filename().string();
    if (name.rfind("docs_", 0) != 0) continue;
    const fs::path program = it->path() / "main.cpp";
    if (!fs::exists(program)) continue;
    programs[name] =
        NormalizeProgram(ReadRepoFile(fs::relative(program, COVERWISE_REPO_ROOT).generic_string()));
  }
  return programs;
}

/// @brief The normalised C++ programs a document ships.
std::vector<std::string> DocumentedPrograms(const std::vector<FencedBlock>& blocks) {
  std::vector<std::string> programs;
  for (const auto& block : blocks) {
    if (block.info != "cpp") continue;
    const std::string body = JoinLines(block.lines);
    if (body.find("int main(") == std::string::npos) continue;
    programs.push_back(NormalizeProgram(body));
  }
  return programs;
}

/// @brief The shape of a document: its heading depths and its fenced blocks, in
///        order, with the prose left out so translations can be compared.
std::vector<std::string> DocumentOutline(const std::vector<std::string>& lines) {
  std::vector<std::string> outline;
  bool inside = false;
  for (const auto& line : lines) {
    const std::string trimmed = Trim(line);
    if (trimmed.rfind("```", 0) == 0) {
      if (!inside) outline.push_back("```" + Trim(trimmed.substr(3)));
      inside = !inside;
      continue;
    }
    if (inside) continue;
    size_t depth = 0;
    while (depth < trimmed.size() && trimmed[depth] == '#') ++depth;
    if (depth > 0 && depth < trimmed.size() && trimmed[depth] == ' ') {
      outline.push_back("h" + std::to_string(depth));
    }
  }
  return outline;
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

/// @brief Every limit the model layer declares, by value.
///
/// The header is read rather than included so a value that moves takes the
/// documents with it: a figure the code no longer produces has nothing here to
/// match. Products of literals are folded, which is how the byte limits are
/// spelled.
std::set<uint64_t> DeclaredLimits() {
  std::set<uint64_t> limits;
  const std::string marker = "inline constexpr";
  for (const auto& line : SplitLines(ReadRepoFile("src/model/limits.h"))) {
    if (line.find(marker) == std::string::npos) continue;
    const size_t assign = line.find('=');
    const size_t end = line.find(';', assign);
    if (assign == std::string::npos || end == std::string::npos) continue;

    uint64_t value = 1;
    std::string digits;
    bool folded = false;
    for (size_t i = assign + 1; i <= end; ++i) {
      const char c = line[i];
      if (std::isdigit(static_cast<unsigned char>(c))) {
        digits.push_back(c);
        continue;
      }
      if (c == '\'') continue;  // A digit separator, not a boundary.
      if (digits.empty()) continue;
      value *= std::stoull(digits);
      digits.clear();
      folded = true;
    }
    if (folded) limits.insert(value);
  }
  return limits;
}

/// @brief The figures a document publishes as limits.
///
/// A limits table names a limit and gives its value: two cells, the first prose
/// and the second nothing but the number, optionally followed by the same number
/// spelled as a unit. A value cell that continues into prose is describing
/// something rather than bounding it, which is what separates a limits table
/// from a feature table or a comparison. Reading the shape rather than the
/// heading keeps every translation on one rule.
std::vector<std::pair<std::string, uint64_t>> PublishedLimits(
    const std::vector<std::string>& lines) {
  std::vector<std::pair<std::string, uint64_t>> published;
  for (const auto& line : lines) {
    const auto cells = TableCells(line);
    if (cells.size() != 2) continue;
    if (cells[0].empty() || cells[1].empty()) continue;
    if (std::isdigit(static_cast<unsigned char>(cells[0].front()))) continue;
    if (!std::isdigit(static_cast<unsigned char>(cells[1].front()))) continue;

    std::string digits;
    size_t i = 0;
    for (; i < cells[1].size(); ++i) {
      const char c = cells[1][i];
      if (std::isdigit(static_cast<unsigned char>(c))) {
        digits.push_back(c);
        continue;
      }
      if (c != ',') break;
    }
    const std::string rest = Trim(cells[1].substr(i));
    if (digits.empty() || (!rest.empty() && rest.front() != '(')) continue;
    published.emplace_back(cells[0], std::stoull(digits));
  }
  return published;
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

// --- Estimate witnesses ---

/// @brief What a model estimates against what generating it actually costs.
struct EstimateWitness {
  uint32_t estimated;
  size_t generated;
};

/// @brief Report the estimate for a model beside the suite generation produces.
EstimateWitness MeasureEstimate(std::vector<Parameter> params, uint32_t strength,
                                std::vector<std::string> constraints = {}) {
  GenerateOptions opts;
  opts.parameters = std::move(params);
  opts.strength = strength;
  opts.constraint_expressions = std::move(constraints);
  opts.seed = 42;

  const auto stats = EstimateModel(opts);
  EXPECT_TRUE(stats.error.ok()) << "Cannot estimate the witness model: " << stats.error.message;
  const auto result = Generate(opts);
  EXPECT_TRUE(result.error.ok()) << "Cannot generate the witness model: " << result.error.message;
  return {stats.estimated_tests, result.tests.size()};
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

/// @brief The counts a document publishes for one configuration.
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

/// @brief Read every benchmark row a document publishes.
///
/// A row is recognised by its configuration cell holding two dimensions joined
/// by a multiplication sign, which is how every document and every translation
/// spells one; a `t-wise` cell in second position names a strength other than
/// pairwise. Recognition is the only thing that passes a line over. Once a line
/// is recognised as a benchmark row, anything it fails to spell out is reported
/// rather than skipped: a row this reader cannot parse is a row whose published
/// numbers nothing compares against the engine, which is the state a published
/// number is most likely to be wrong in.
std::map<BenchmarkKey, BenchmarkClaim> PublishedBenchmarks(const std::vector<std::string>& lines,
                                                           const std::string& document) {
  std::map<BenchmarkKey, BenchmarkClaim> rows;
  for (const auto& line : lines) {
    const auto cells = TableCells(line);
    if (cells.empty()) continue;
    if (cells[0].find(kMultiplicationSign) == std::string::npos) continue;

    const std::string row = document + " benchmark row `" + Trim(line) + "`";
    if (cells.size() < 4) {
      ADD_FAILURE() << row << " has " << cells.size()
                    << " cells; a benchmark row names a configuration, a tuple count and a test "
                       "count, and names its strength when it is not pairwise";
      continue;
    }

    const auto dimensions = LeadingIntegers(cells[0]);
    if (dimensions.size() != 2) {
      ADD_FAILURE() << row << " does not name a parameter count and a value count";
      continue;
    }

    uint32_t strength = 2;
    size_t tuples_cell = 1;
    const size_t wise = cells[1].find("-wise");
    if (wise != std::string::npos) {
      if (!ParseCount(cells[1].substr(0, wise), &strength)) {
        ADD_FAILURE() << row << " names a strength this reader cannot read: " << cells[1];
        continue;
      }
      tuples_cell = 2;
    }

    BenchmarkClaim claim{};
    if (!ParseCount(cells[tuples_cell], &claim.tuples)) {
      ADD_FAILURE() << row << " does not publish a tuple count: " << cells[tuples_cell];
      continue;
    }
    if (!ParseCount(cells[tuples_cell + 1], &claim.tests)) {
      ADD_FAILURE() << row << " does not publish a test count: " << cells[tuples_cell + 1];
      continue;
    }

    const BenchmarkKey key{dimensions[0], dimensions[1], strength};
    EXPECT_EQ(rows.count(key), 0u) << document << " lists the same configuration twice";
    rows[key] = claim;
  }
  return rows;
}

std::string FoldAscii(const std::string& value) {
  std::string folded = value;
  for (auto& c : folded) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
  }
  return folded;
}

// --- What a document says that this gate checks ---

/// @brief Everything one document states that is derived from the code.
struct DocumentClaims {
  size_t constraint_expressions = 0;
  size_t inventory_rows = 0;
  size_t benchmark_rows = 0;
  size_t value_format_names = 0;
  size_t suite_rows = 0;
  size_t programs = 0;

  size_t total() const {
    return constraint_expressions + inventory_rows + benchmark_rows + value_format_names +
           suite_rows + programs;
  }
};

DocumentClaims CollectClaims(const std::string& document) {
  const auto lines = SplitLines(ReadRepoFile(document));
  const auto blocks = FencedBlocks(lines);

  DocumentClaims claims;
  claims.constraint_expressions = DocumentedConstraintExpressions(blocks).size();
  claims.inventory_rows = DocumentedKeywords(lines).size() + DocumentedWildcards(lines).size() +
                          DocumentedBuilderMethods(lines).size();
  claims.benchmark_rows = PublishedBenchmarks(lines, document).size();
  claims.value_format_names = ValueFormatSampleNames(blocks).size();
  for (const auto& listing : DocumentedSuiteListings(blocks)) {
    claims.suite_rows += listing.rows.size();
  }
  claims.programs = DocumentedPrograms(blocks).size();
  return claims;
}

}  // namespace

// --- The gate's own target set ---

TEST(DocsContractTest, EveryShippedDocumentIsEitherCheckedOrExplained) {
  const auto shipped = ShippedDocuments();
  ASSERT_GE(shipped.size(), 2u) << "The documentation tree could not be walked";

  for (const auto& document : shipped) {
    EXPECT_FALSE(IsUnchecked(document) && IsCheckedElsewhere(document))
        << document << " is recorded both as claim-free and as checked elsewhere";

    const auto claims = CollectClaims(document);
    if (IsCheckedElsewhere(document)) continue;
    if (IsUnchecked(document)) {
      EXPECT_EQ(claims.total(), 0u)
          << document
          << " is listed as carrying nothing to check, but it now states something this gate "
             "reads; remove the entry so the document is checked";
      continue;
    }
    EXPECT_GT(claims.total(), 0u)
        << document
        << " states nothing this gate can check against the code. Either give it a sample, an "
           "inventory or a measurement that is checked, or record why it carries no such claim, or "
           "record which test elsewhere checks it";
  }
}

TEST(DocsContractTest, EveryDelegatedDocumentNamesACheckerThatExists) {
  for (const auto& entry : kDocumentsCheckedElsewhere) {
    const std::string path = std::string(COVERWISE_REPO_ROOT) + "/" + entry.checker;
    EXPECT_TRUE(std::filesystem::exists(path))
        << entry.path << " is recorded as checked by " << entry.checker
        << ", which no longer exists; the document is now checked by nothing";
  }
}

TEST(DocsContractTest, EveryCategorisedDocumentStillExists) {
  const auto shipped = ShippedDocuments();
  for (const auto& entry : kDocumentsWithoutCheckableClaims) {
    EXPECT_NE(std::find(shipped.begin(), shipped.end(), entry.path), shipped.end())
        << entry.path << " is recorded as carrying no checkable claim but is no longer shipped";
  }
  for (const auto& entry : kDocumentsCheckedElsewhere) {
    EXPECT_NE(std::find(shipped.begin(), shipped.end(), entry.path), shipped.end())
        << entry.path << " is recorded as checked by " << entry.checker
        << " but is no longer shipped";
  }
}

TEST(DocsContractTest, EveryDocumentIsWrittenInEveryLanguage) {
  const auto languages = DocumentLanguages();
  ASSERT_FALSE(languages.empty()) << "The documentation tree holds no language directory";

  for (const auto& [name, translations] : TranslationGroups()) {
    for (const auto& language : languages) {
      EXPECT_EQ(translations.count(language), 1u) << name << " is not written in " << language;
    }
  }
}

TEST(DocsContractTest, TranslationsAreStructuredAlike) {
  for (const auto& group : TranslationGroups()) {
    const std::string* reference_path = nullptr;
    std::vector<std::string> reference;
    for (const auto& translation : group.second) {
      const std::string& path = translation.second;
      const auto outline = DocumentOutline(SplitLines(ReadRepoFile(path)));
      if (reference_path == nullptr) {
        reference_path = &path;
        reference = outline;
        continue;
      }
      EXPECT_EQ(outline, reference)
          << path << " does not hold the same sections and code blocks as " << *reference_path;
    }
  }
}

// --- Constraint samples ---

TEST(DocsContractTest, EveryDocumentedConstraintExpressionBehavesAsShown) {
  const auto params = DocumentationModel();
  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    const auto claims =
        DocumentedConstraintExpressions(FencedBlocks(SplitLines(ReadRepoFile(document))));
    for (const auto& claim : claims) {
      auto result = ParseConstraint(claim.expression, params);
      ++checked;
      if (claim.shown_as_rejected) {
        EXPECT_FALSE(result.error.ok())
            << document
            << " shows the parser rejecting an expression it accepts: " << claim.expression;
        continue;
      }
      EXPECT_TRUE(result.error.ok())
          << document << " shows an expression the parser rejects: " << claim.expression << " -- "
          << result.error.message;
      EXPECT_NE(result.constraint, nullptr) << document << ": " << claim.expression;
    }
  }
  EXPECT_GT(checked, 0u) << "No document shows a constraint expression";
}

TEST(DocsContractTest, TranslationsShowTheSameConstraintExpressions) {
  for (const auto& group : TranslationGroups()) {
    const std::string* reference_path = nullptr;
    std::vector<std::string> reference;
    for (const auto& translation : group.second) {
      const std::string& path = translation.second;
      if (IsUnchecked(path)) continue;
      std::vector<std::string> expressions;
      for (const auto& claim :
           DocumentedConstraintExpressions(FencedBlocks(SplitLines(ReadRepoFile(path))))) {
        expressions.push_back(claim.expression);
      }
      if (reference_path == nullptr) {
        reference_path = &path;
        reference = std::move(expressions);
        continue;
      }
      EXPECT_EQ(expressions, reference) << path << " diverges from " << *reference_path;
    }
  }
}

// --- Parameter validation contract ---

TEST(DocsContractTest, TheDocumentedValueFormatSampleIsAccepted) {
  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    const auto names = ValueFormatSampleNames(FencedBlocks(SplitLines(ReadRepoFile(document))));
    if (names.empty()) continue;
    ++checked;

    std::set<std::string> folded;
    for (const auto& name : names) {
      EXPECT_TRUE(folded.insert(FoldAscii(name)).second)
          << document << " shows a sample whose names collide once ASCII case is folded: " << name;
    }
  }
  EXPECT_GT(checked, 0u) << "No document shows a value-format sample";
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
  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    const auto documented = DocumentedKeywords(SplitLines(ReadRepoFile(document)));
    if (documented.empty()) continue;
    ++checked;
    EXPECT_EQ(documented, keywords)
        << document << " does not list exactly the keywords the parser accepts";
  }
  EXPECT_GT(checked, 0u) << "No document lists the constraint keywords";
}

TEST(DocsContractTest, DocumentedWildcardsMatchTheGlobMatcher) {
  const auto wildcards = GlobMatcherWildcards();
  ASSERT_FALSE(wildcards.empty()) << "Could not read the glob matcher wildcards";
  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    const auto documented = DocumentedWildcards(SplitLines(ReadRepoFile(document)));
    if (documented.empty()) continue;
    ++checked;
    EXPECT_EQ(documented, wildcards)
        << document << " does not list exactly the wildcards LIKE honors";
  }
  EXPECT_GT(checked, 0u) << "No document lists the LIKE wildcards";
}

TEST(DocsContractTest, DocumentedBuilderMethodsMatchTheBuilderInterface) {
  const auto methods = BuilderInterfaceMethods();
  ASSERT_FALSE(methods.empty()) << "Could not read the builder interfaces";
  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    const auto documented = DocumentedBuilderMethods(SplitLines(ReadRepoFile(document)));
    if (documented.empty()) continue;
    ++checked;
    EXPECT_EQ(documented, methods)
        << document << " does not list exactly the builder's public methods";
  }
  EXPECT_GT(checked, 0u) << "No document lists the builder's methods";
}

// --- Published limit figures ---

TEST(DocsContractTest, EveryPublishedLimitIsOneTheCodeStillProduces) {
  const auto declared = DeclaredLimits();
  ASSERT_FALSE(declared.empty()) << "Could not read the declared limits";

  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    for (const auto& [name, value] : PublishedLimits(SplitLines(ReadRepoFile(document)))) {
      ++checked;
      EXPECT_EQ(declared.count(value), 1u)
          << document << " publishes " << value << " as the limit on " << name
          << ", which the model layer no longer declares";
    }
  }
  EXPECT_GT(checked, 0u) << "No document publishes a limit";
}

// --- Published benchmark numbers ---

TEST(DocsContractTest, EveryDocumentPublishesTheSameBenchmarkTable) {
  std::string reference_document;
  std::map<BenchmarkKey, BenchmarkClaim> reference;

  for (const auto& document : GatedDocuments()) {
    const auto rows = PublishedBenchmarks(SplitLines(ReadRepoFile(document)), document);
    if (rows.empty()) continue;
    if (reference_document.empty()) {
      reference_document = document;
      reference = rows;
      continue;
    }
    ASSERT_EQ(rows.size(), reference.size()) << document << " publishes a different row count";
    for (const auto& [key, claim] : reference) {
      const auto found = rows.find(key);
      ASSERT_NE(found, rows.end()) << document << " omits " << key.parameters << " parameters of "
                                   << key.values << " values at strength " << key.strength;
      EXPECT_EQ(found->second.tuples, claim.tuples)
          << document << " disagrees with " << reference_document << " on the tuple count of "
          << key.parameters << " parameters of " << key.values << " values at strength "
          << key.strength;
      EXPECT_EQ(found->second.tests, claim.tests)
          << document << " disagrees with " << reference_document << " on the test count of "
          << key.parameters << " parameters of " << key.values << " values at strength "
          << key.strength;
    }
  }
  EXPECT_FALSE(reference_document.empty()) << "No document publishes a benchmark table";
}

TEST(DocsContractTest, PublishedBenchmarkCountsMatchGeneratedSuites) {
  for (const auto& document : GatedDocuments()) {
    for (const auto& [key, claim] :
         PublishedBenchmarks(SplitLines(ReadRepoFile(document)), document)) {
      GenerateOptions opts;
      opts.parameters = MakeUniformParams(key.parameters, key.values);
      opts.strength = key.strength;
      opts.seed = 42;

      auto result = Generate(opts);
      EXPECT_EQ(result.stats.total_tuples, claim.tuples)
          << document << " publishes a stale tuple count for " << key.parameters
          << " parameters of " << key.values << " values at strength " << key.strength;
      EXPECT_EQ(result.tests.size(), claim.tests)
          << document << " publishes a stale test count for " << key.parameters << " parameters of "
          << key.values << " values at strength " << key.strength;
    }
  }
}

// --- Printed suites ---

TEST(DocsContractTest, EveryPrintedTestCaseIsTheSuiteMemberItIsDrawnAs) {
  size_t checked = 0;
  for (const auto& document : GatedDocuments()) {
    const auto listings = DocumentedSuiteListings(FencedBlocks(SplitLines(ReadRepoFile(document))));
    for (const auto& listing : listings) {
      GenerateOptions opts;
      opts.parameters = listing.parameters;
      opts.constraint_expressions = listing.constraints;
      opts.strength = listing.strength;
      opts.seed = listing.seed;

      const auto result = Generate(opts);
      ASSERT_TRUE(result.error.ok())
          << document << " shows a model the engine rejects: " << result.error.message;
      ASSERT_EQ(result.parameters.size(), listing.parameters.size())
          << document << " shows a model whose parameters the engine does not return as written";

      for (size_t row = 0; row < listing.rows.size(); ++row) {
        ASSERT_LT(row, result.tests.size())
            << document << " prints " << listing.rows.size() << " test cases for a model that "
            << "generates " << result.tests.size();
        ASSERT_EQ(listing.rows[row].size(), listing.parameters.size())
            << document << " prints a test case that does not name every parameter";
        for (size_t i = 0; i < listing.parameters.size(); ++i) {
          const auto& parameter = result.parameters[i];
          EXPECT_EQ(parameter.values[result.tests[row].values[i]], listing.rows[row][i])
              << document << " prints " << parameter.name << "=" << listing.rows[row][i]
              << " as test case " << row << ", which the shown model does not produce there";
        }
        ++checked;
      }
    }
  }
  EXPECT_GT(checked, 0u) << "No document prints a generated test case";
}

// --- Shipped C++ programs ---

TEST(DocsContractTest, EveryCompiledExampleIsShippedInEveryLanguage) {
  const auto fixtures = DocumentationFixturePrograms();
  ASSERT_FALSE(fixtures.empty()) << "No documentation fixture program was found";

  std::map<std::string, std::vector<std::string>> by_language;
  for (const auto& group : TranslationGroups()) {
    for (const auto& [language, path] : group.second) {
      for (auto& program : DocumentedPrograms(FencedBlocks(SplitLines(ReadRepoFile(path))))) {
        by_language[language].push_back(std::move(program));
      }
    }
  }

  for (const auto& language : DocumentLanguages()) {
    const auto& programs = by_language[language];
    for (const auto& [fixture, program] : fixtures) {
      EXPECT_NE(std::find(programs.begin(), programs.end(), program), programs.end())
          << "The program the " << fixture << " fixture compiles is not shipped in " << language
          << "; the documented example is no longer the text that compiles";
    }
  }
}

// --- Model size estimate ---

TEST(DocsContractTest, TheDocumentedModelSizeEstimateBoundsNothing) {
  // The documentation describes estimatedTests as a sizing heuristic that bounds
  // the suite in neither direction. Both directions are witnessed by generating,
  // so the wording cannot outlive the behaviour it describes. Should either
  // witness fail, the estimate no longer falls on that side for that model:
  // replace it with one that does, or -- if none does -- strengthen the
  // documented claim to the bound the new measurement supports.
  const auto larger = MeasureEstimate(MakeUniformParams(12, 3), 6);
  EXPECT_GT(larger.generated, larger.estimated)
      << "12 parameters of 3 values at strength 6 estimates " << larger.estimated
      << " tests and generates " << larger.generated
      << ", so the estimate no longer undershoots a generated suite";

  const auto smaller = MeasureEstimate({{"os", {"Windows", "macOS", "Linux"}},
                                        {"browser", {"Chrome", "Firefox", "Safari"}},
                                        {"theme", {"light", "dark"}}},
                                       2, {"IF os = Windows THEN browser != Safari"});
  EXPECT_LT(smaller.generated, smaller.estimated)
      << "the model the CLI document shows estimates " << smaller.estimated
      << " tests and generates " << smaller.generated
      << ", so the estimate no longer overshoots a generated suite";
}
