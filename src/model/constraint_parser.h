/// @file constraint_parser.h
/// @brief Parser for human-readable constraint expressions.

#ifndef COVERWISE_MODEL_CONSTRAINT_PARSER_H_
#define COVERWISE_MODEL_CONSTRAINT_PARSER_H_

#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/error.h"
#include "model/parameter.h"

namespace coverwise {
namespace model {

/// @brief Result of parsing a constraint expression.
struct ParseResult {
  Constraint constraint;  ///< Root AST node, nullptr on error.
  Error error;            ///< ok() if parsing succeeded.
};

/// @brief Options controlling constraint parsing behavior.
struct ParseOptions {
  /// @brief When false (default), parameter and value name resolution
  ///        is case-insensitive. When true, exact match is required.
  bool case_sensitive = false;
};

/// @brief Parse a human-readable constraint expression into an AST.
///
/// Supported syntax examples:
///   "IF os=mac THEN browser!=ie"
///   "IF os=mac THEN browser!=ie ELSE arch!=arm"
///   "NOT (os=win AND browser=safari)"
///   "os=linux IMPLIES arch!=arm"
///   "os=win OR browser=chrome"
///   "NOT os=linux"
///   "version > 3"
///   "env IN {staging, prod}"
///   "browser LIKE chrome*"
///   "start_date < end_date"   (parameter-to-parameter comparison)
///
/// Keywords (case-insensitive): IF, THEN, ELSE, IMPLIES, AND, OR, NOT, IN, LIKE
/// Operators: = != > >= < <=
/// Parentheses: ( )
/// Set literals: { value1, value2, ... }
///
/// @param expression The constraint string to parse.
/// @param params The parameter definitions (used to resolve names to indices).
/// @param options Parsing options (e.g., case sensitivity).
/// @return ParseResult with the AST on success, or an Error on failure.
ParseResult ParseConstraint(const std::string& expression, const std::vector<Parameter>& params,
                            const ParseOptions& options = {});

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_CONSTRAINT_PARSER_H_
