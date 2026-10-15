/// @file constraint_lexer.h
/// @brief Tokenizer for constraint expressions (internal to the constraint parser).

#ifndef COVERWISE_MODEL_CONSTRAINT_LEXER_H_
#define COVERWISE_MODEL_CONSTRAINT_LEXER_H_

#include <cstddef>
#include <string>
#include <vector>

#include "model/error.h"

namespace coverwise {
namespace model {
namespace detail {

inline constexpr size_t kMaxAstDepth = 128;

// --- Token types ---

enum class TokenType {
  kIdentifier,
  kNumber,        // numeric literal (42, 3.14, -1)
  kEquals,        // =
  kNotEquals,     // !=
  kLess,          // <
  kLessEqual,     // <=
  kGreater,       // >
  kGreaterEqual,  // >=
  kLParen,        // (
  kRParen,        // )
  kLBrace,        // {
  kRBrace,        // }
  kComma,         // ,
  kAnd,           // AND
  kOr,            // OR
  kNot,           // NOT
  kIf,            // IF
  kThen,          // THEN
  kElse,          // ELSE
  kImplies,       // IMPLIES
  kIn,            // IN
  kLike,          // LIKE
  kEnd,           // end of input
};

struct Token {
  TokenType type;
  std::string text;
  size_t position;          // Unicode codepoint offset in the original expression
  bool was_quoted = false;  // True if this identifier came from a quoted string literal.
};

struct TokenizeResult {
  std::vector<Token> tokens;
  Error error;
};

TokenizeResult Tokenize(const std::string& expr);

}  // namespace detail
}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_CONSTRAINT_LEXER_H_
