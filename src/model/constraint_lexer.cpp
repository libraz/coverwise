#include "model/constraint_lexer.h"

#include <string>
#include <utility>
#include <vector>

#include "util/string_util.h"

namespace coverwise {
namespace model {
namespace detail {
namespace {

constexpr size_t kMaxTokens = 4096;

// --- Tokenizer ---

bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

bool IsAsciiAlphaNumeric(char c) {
  return IsAsciiDigit(c) || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool IsIdentChar(char c) {
  return IsAsciiAlphaNumeric(c) || c == '_' || c == '-' || c == '.' ||
         (static_cast<unsigned char>(c) >= 0x80);
}

bool IsGlobPatternChar(char c) { return IsIdentChar(c) || c == '*' || c == '?' || c == '.'; }

std::vector<size_t> BuildCodepointPositions(const std::string& value) {
  std::vector<size_t> positions(value.size() + 1);
  size_t codepoints = 0;
  for (size_t i = 0; i < value.size(); ++i) {
    positions[i] = codepoints;
    if ((static_cast<unsigned char>(value[i]) & 0xC0) != 0x80) {
      ++codepoints;
    }
  }
  positions[value.size()] = codepoints;
  return positions;
}

bool IsComparisonToken(TokenType type) {
  return type == TokenType::kEquals || type == TokenType::kNotEquals || type == TokenType::kLess ||
         type == TokenType::kLessEqual || type == TokenType::kGreater ||
         type == TokenType::kGreaterEqual;
}

/// Scan the longest prefix accepted by the shared strict decimal grammar.
size_t ScanDecimal(const std::string& value, size_t start, bool allow_sign_or_leading_dot) {
  size_t i = start;
  if (allow_sign_or_leading_dot && i < value.size() && (value[i] == '+' || value[i] == '-')) {
    ++i;
  }

  size_t digits = 0;
  while (i < value.size() && IsAsciiDigit(value[i])) {
    ++digits;
    ++i;
  }
  if (i < value.size() && value[i] == '.') {
    ++i;
    while (i < value.size() && IsAsciiDigit(value[i])) {
      ++digits;
      ++i;
    }
  }
  if (digits == 0) return start;

  if (i < value.size() && (value[i] == 'e' || value[i] == 'E')) {
    const size_t exponent_start = i;
    ++i;
    if (i < value.size() && (value[i] == '+' || value[i] == '-')) ++i;
    const size_t exponent_digits_start = i;
    while (i < value.size() && IsAsciiDigit(value[i])) ++i;
    if (i == exponent_digits_start) return exponent_start;
  }
  return i;
}

/// @brief Classify a word already run through util::FoldAsciiString.
///
/// Keyword matching is case-insensitive, and it is case-insensitive by the one
/// fold the rest of the engine uses rather than a second rule of its own.
TokenType ClassifyKeyword(const std::string& upper) {
  if (upper == "AND") return TokenType::kAnd;
  if (upper == "OR") return TokenType::kOr;
  if (upper == "NOT") return TokenType::kNot;
  if (upper == "IF") return TokenType::kIf;
  if (upper == "THEN") return TokenType::kThen;
  if (upper == "ELSE") return TokenType::kElse;
  if (upper == "IMPLIES") return TokenType::kImplies;
  if (upper == "IN") return TokenType::kIn;
  if (upper == "LIKE") return TokenType::kLike;
  return TokenType::kIdentifier;
}

}  // namespace

TokenizeResult Tokenize(const std::string& expr) {
  std::vector<Token> tokens;
  size_t i = 0;
  size_t len = expr.size();
  bool expect_pattern = false;
  const auto codepoint_positions = BuildCodepointPositions(expr);

  while (i < len) {
    // Explicit ASCII whitespace set (space, tab, LF, CR), matching the
    // TypeScript tokenizer. Avoids locale-dependent std::isspace, which also
    // treats \v and \f as space and would diverge from the pure-JS surface.
    const char c = expr[i];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      ++i;
      continue;
    }

    if (tokens.size() >= kMaxTokens) {
      return {{},
              {Error::Code::kConstraintError,
               "Constraint token limit exceeded (maximum " + std::to_string(kMaxTokens) + ")",
               "Simplify or split the constraint expression"}};
    }

    // Positions were computed once while scanning, avoiding a prefix rescan per token.
    const size_t start = codepoint_positions[i];

    if (expr[i] == '(') {
      tokens.push_back({TokenType::kLParen, "(", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == ')') {
      tokens.push_back({TokenType::kRParen, ")", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '{') {
      tokens.push_back({TokenType::kLBrace, "{", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '}') {
      tokens.push_back({TokenType::kRBrace, "}", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == ',') {
      tokens.push_back({TokenType::kComma, ",", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '!' && i + 1 < len && expr[i + 1] == '=') {
      tokens.push_back({TokenType::kNotEquals, "!=", start});
      i += 2;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '<' && i + 1 < len && expr[i + 1] == '=') {
      tokens.push_back({TokenType::kLessEqual, "<=", start});
      i += 2;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '>' && i + 1 < len && expr[i + 1] == '=') {
      tokens.push_back({TokenType::kGreaterEqual, ">=", start});
      i += 2;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '<') {
      tokens.push_back({TokenType::kLess, "<", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '>') {
      tokens.push_back({TokenType::kGreater, ">", start});
      ++i;
      expect_pattern = false;
      continue;
    }
    if (expr[i] == '=') {
      tokens.push_back({TokenType::kEquals, "=", start});
      ++i;
      expect_pattern = false;
      continue;
    }

    // LIKE pattern: after the LIKE keyword, consume a glob pattern. This must be
    // checked before the number branch so a pattern that starts with a digit
    // (e.g. version LIKE 1.*) is read as a pattern rather than a decimal literal
    // that then chokes on '*'.
    if (expect_pattern) {
      size_t j = i;
      while (j < len && IsGlobPatternChar(expr[j])) {
        ++j;
      }
      if (j > i) {
        std::string pattern = expr.substr(i, j - i);
        tokens.push_back({TokenType::kIdentifier, pattern, start});
        i = j;
        expect_pattern = false;
        continue;
      }
    }

    // Number literal. Signs and a leading dot are accepted only after an operator,
    // preserving string values such as "-1" inside IN sets as identifiers.
    const bool after_comparison = !tokens.empty() && IsComparisonToken(tokens.back().type);
    const bool numeric_start =
        IsAsciiDigit(expr[i]) ||
        (after_comparison && ((expr[i] == '+' || expr[i] == '-') ||
                              (expr[i] == '.' && i + 1 < len && IsAsciiDigit(expr[i + 1]))));
    if (numeric_start) {
      size_t j = ScanDecimal(expr, i, after_comparison);
      // If followed by identifier chars, it's actually an identifier (e.g., "3d")
      if (j > i && j < len && IsIdentChar(expr[j]) && expr[i] != '+') {
        j = i;
        while (j < len && IsIdentChar(expr[j])) {
          ++j;
        }
        std::string word = expr.substr(i, j - i);
        tokens.push_back({TokenType::kIdentifier, word, start});
      } else if (j > i) {
        std::string num = expr.substr(i, j - i);
        tokens.push_back({TokenType::kNumber, num, start});
      } else if (expr[i] != '+' && IsIdentChar(expr[i])) {
        // ScanDecimal found no number (e.g. a value like "-foo" after '='). Treat
        // it as an identifier value, matching how the same token is accepted
        // inside an IN set, instead of rejecting a non-numeric value that starts
        // with a sign.
        j = i;
        while (j < len && IsIdentChar(expr[j])) {
          ++j;
        }
        tokens.push_back({TokenType::kIdentifier, expr.substr(i, j - i), start});
      } else {
        return {{},
                {Error::Code::kConstraintError,
                 "Invalid decimal literal at position " + std::to_string(start),
                 "Expected a finite decimal number"}};
      }
      i = j;
      expect_pattern = false;
      continue;
    }

    // Quoted string: "..." or '...'. Supports backslash escapes \" and \\ so a
    // quoted value can contain its own quote character or a literal backslash.
    if (expr[i] == '"' || expr[i] == '\'') {
      char quote = expr[i];
      size_t j = i + 1;
      std::string content;
      bool terminated = false;
      while (j < len) {
        char c = expr[j];
        if (c == '\\' && j + 1 < len && (expr[j + 1] == quote || expr[j + 1] == '\\')) {
          content.push_back(expr[j + 1]);
          j += 2;
          continue;
        }
        if (c == quote) {
          terminated = true;
          break;
        }
        content.push_back(c);
        ++j;
      }
      if (!terminated) {
        return {{},
                {Error::Code::kConstraintError,
                 "Unterminated string literal starting at position " + std::to_string(start), ""}};
      }
      tokens.push_back({TokenType::kIdentifier, content, start, /*was_quoted=*/true});
      i = j + 1;
      expect_pattern = false;
      continue;
    }

    if (IsIdentChar(expr[i])) {
      size_t j = i;
      while (j < len && IsIdentChar(expr[j])) {
        ++j;
      }
      std::string word = expr.substr(i, j - i);
      TokenType type = ClassifyKeyword(util::FoldAsciiString(word));
      tokens.push_back({type, word, start});
      i = j;
      expect_pattern = (type == TokenType::kLike);
      continue;
    }

    // Handle glob pattern chars at top level (e.g., after LIKE if expect_pattern missed)
    if (expr[i] == '*' || expr[i] == '?') {
      size_t j = i;
      while (j < len && IsGlobPatternChar(expr[j])) {
        ++j;
      }
      std::string pattern = expr.substr(i, j - i);
      tokens.push_back({TokenType::kIdentifier, pattern, start});
      i = j;
      expect_pattern = false;
      continue;
    }

    return {{},
            {Error::Code::kConstraintError,
             "Unexpected character '" + std::string(1, expr[i]) + "' at position " +
                 std::to_string(start),
             ""}};
  }

  // Bound only the actual nesting depth (parenthesis nesting), not the total
  // count of operators. A flat expression such as 200 OR-connected clauses has
  // shallow nesting and must be accepted; deep NOT recursion is bounded inside
  // the recursive-descent parser itself (see ParseExpression / ParseUnaryExpr).
  size_t paren_depth = 0;
  for (const auto& token : tokens) {
    if (token.type == TokenType::kLParen) {
      ++paren_depth;
      if (paren_depth > kMaxAstDepth) {
        return {{},
                {Error::Code::kConstraintError,
                 "Constraint nesting depth limit exceeded (maximum " +
                     std::to_string(kMaxAstDepth) + ")",
                 "Simplify or split the constraint expression"}};
      }
    } else if (token.type == TokenType::kRParen && paren_depth > 0) {
      --paren_depth;
    }
  }

  tokens.push_back({TokenType::kEnd, "", codepoint_positions[len]});
  return {std::move(tokens), {}};
}

}  // namespace detail
}  // namespace model
}  // namespace coverwise
