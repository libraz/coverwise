#include "model/constraint_parser.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "model/constraint_lexer.h"
#include "util/string_util.h"

namespace coverwise {
namespace model {

using detail::kMaxAstDepth;
using detail::Token;
using detail::Tokenize;
using detail::TokenType;

namespace {

constexpr size_t kMaxExpressionBytes = 64 * 1024;
constexpr size_t kMaxAstNodes = 1024;

// --- Name resolution ---

/// @brief Compare two strings, optionally case-insensitive.
bool NamesEqual(const std::string& a, const std::string& b, bool case_sensitive) {
  if (case_sensitive) {
    return a == b;
  }
  return util::CaseInsensitiveEqual(a, b);
}

struct ResolvedParam {
  uint32_t param_index;
  Error error;
};

ResolvedParam ResolveParam(const std::string& param_name, const std::vector<Parameter>& params,
                           bool case_sensitive) {
  for (uint32_t i = 0; i < static_cast<uint32_t>(params.size()); ++i) {
    if (NamesEqual(params[i].name, param_name, case_sensitive)) {
      return {i, {}};
    }
  }
  std::string available;
  for (size_t i = 0; i < params.size(); ++i) {
    if (i > 0) available += ", ";
    available += params[i].name;
  }
  return {0,
          {Error::Code::kConstraintError, "Unknown parameter '" + param_name + "'",
           "Available parameters: " + available}};
}

struct ResolvedComparison {
  uint32_t param_index;
  uint32_t value_index;
  Error error;
};

ResolvedComparison ResolveComparison(const std::string& param_name, const std::string& value_name,
                                     const std::vector<Parameter>& params, bool case_sensitive) {
  auto rp = ResolveParam(param_name, params, case_sensitive);
  if (!rp.error.ok()) {
    return {0, 0, rp.error};
  }
  uint32_t param_idx = rp.param_index;

  uint32_t val_idx = params[param_idx].find_value_index(value_name, case_sensitive);
  if (val_idx == UINT32_MAX) {
    std::string available;
    const auto& values = params[param_idx].values;
    for (size_t i = 0; i < values.size(); ++i) {
      if (i > 0) available += ", ";
      available += values[i];
    }
    return {0,
            0,
            {Error::Code::kConstraintError,
             "Unknown value '" + value_name + "' for parameter '" + param_name + "'",
             "Available values: " + available}};
  }

  return {param_idx, val_idx, {}};
}

/// @brief Resolve a value name within a specific parameter.
struct ResolvedValue {
  uint32_t value_index;
  Error error;
};

ResolvedValue ResolveValue(uint32_t param_index, const std::string& value_name,
                           const std::vector<Parameter>& params, bool case_sensitive) {
  uint32_t idx = params[param_index].find_value_index(value_name, case_sensitive);
  if (idx != UINT32_MAX) {
    return {idx, {}};
  }
  std::string available;
  const auto& values = params[param_index].values;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) available += ", ";
    available += values[i];
  }
  return {0,
          {Error::Code::kConstraintError,
           "Unknown value '" + value_name + "' for parameter '" + params[param_index].name + "'",
           "Available values: " + available}};
}

/// @brief Check if a name refers to a known parameter.
bool IsParameterName(const std::string& name, const std::vector<Parameter>& params,
                     bool case_sensitive) {
  for (const auto& p : params) {
    if (NamesEqual(p.name, name, case_sensitive)) {
      return true;
    }
  }
  return false;
}

/// @brief Check if a name is a value (or alias) of the given parameter.
bool IsValueOfParam(uint32_t param_index, const std::string& name,
                    const std::vector<Parameter>& params, bool case_sensitive) {
  return params[param_index].find_value_index(name, case_sensitive) != UINT32_MAX;
}

// --- Recursive descent parser ---

class Parser {
 public:
  Parser(std::vector<Token> tokens, const std::vector<Parameter>& params,
         const ParseOptions& options)
      : tokens_(std::move(tokens)),
        params_(params),
        options_(options),
        numeric_caches_(params.size()),
        pos_(0) {}

  ParseResult Parse() {
    auto result = ParseExpression();
    if (!result.error.ok()) {
      return result;
    }
    if (Current().type != TokenType::kEnd) {
      return {nullptr,
              {Error::Code::kConstraintError,
               "Unexpected token '" + Current().text + "' at position " +
                   std::to_string(Current().position),
               "Expected end of expression"}};
    }
    return result;
  }

 private:
  NumericValueCachePtr NumericCacheFor(uint32_t param_index) {
    auto& cache = numeric_caches_[param_index];
    if (!cache) cache = BuildNumericValueCache(params_[param_index].values);
    return cache;
  }

  const Token& Current() const { return tokens_[pos_]; }

  const Token& Advance() {
    const Token& tok = tokens_[pos_];
    if (pos_ + 1 < tokens_.size()) {
      ++pos_;
    }
    return tok;
  }

  bool Match(TokenType type) {
    if (Current().type == type) {
      Advance();
      return true;
    }
    return false;
  }

  ParseResult MakeNode(Constraint constraint) {
    ++node_count_;
    if (node_count_ > kMaxAstNodes) {
      return {nullptr,
              {Error::Code::kConstraintError,
               "Constraint AST node limit exceeded (maximum " + std::to_string(kMaxAstNodes) + ")",
               "Simplify or split the constraint expression"}};
    }
    return {std::move(constraint), {}};
  }

  ParseResult BuildBalanced(std::vector<Constraint>* terms, size_t begin, size_t end, bool is_and) {
    if (end - begin == 1) return {std::move((*terms)[begin]), {}};
    const size_t middle = begin + (end - begin) / 2;
    auto left = BuildBalanced(terms, begin, middle, is_and);
    if (!left.error.ok()) return left;
    auto right = BuildBalanced(terms, middle, end, is_and);
    if (!right.error.ok()) return right;
    if (is_and) {
      return MakeNode(
          std::make_unique<AndNode>(std::move(left.constraint), std::move(right.constraint)));
    }
    return MakeNode(
        std::make_unique<OrNode>(std::move(left.constraint), std::move(right.constraint)));
  }

  ParseResult ParseExpression() { return ParseImpliesExpr(); }

  ParseResult ParseImpliesExpr() {
    if (Current().type == TokenType::kIf) {
      Advance();
      auto antecedent = ParseOrExpr();
      if (!antecedent.error.ok()) {
        return antecedent;
      }
      if (Current().type != TokenType::kThen) {
        return {
            nullptr,
            {Error::Code::kConstraintError,
             "Expected 'THEN' after 'IF' clause at position " + std::to_string(Current().position),
             "Syntax: IF <condition> THEN <condition>"}};
      }
      Advance();
      auto consequent = ParseOrExpr();
      if (!consequent.error.ok()) {
        return consequent;
      }

      // Check for optional ELSE clause
      if (Current().type == TokenType::kElse) {
        Advance();
        auto else_branch = ParseOrExpr();
        if (!else_branch.error.ok()) {
          return else_branch;
        }
        return MakeNode(std::make_unique<IfThenElseNode>(std::move(antecedent.constraint),
                                                         std::move(consequent.constraint),
                                                         std::move(else_branch.constraint)));
      }

      return MakeNode(std::make_unique<ImpliesNode>(std::move(antecedent.constraint),
                                                    std::move(consequent.constraint)));
    }

    auto left = ParseOrExpr();
    if (!left.error.ok()) {
      return left;
    }

    if (Match(TokenType::kImplies)) {
      auto right = ParseOrExpr();
      if (!right.error.ok()) {
        return right;
      }
      return MakeNode(
          std::make_unique<ImpliesNode>(std::move(left.constraint), std::move(right.constraint)));
    }

    return left;
  }

  ParseResult ParseOrExpr() {
    auto first = ParseAndExpr();
    if (!first.error.ok()) {
      return first;
    }
    std::vector<Constraint> terms;
    terms.push_back(std::move(first.constraint));

    while (Match(TokenType::kOr)) {
      auto right = ParseAndExpr();
      if (!right.error.ok()) {
        return right;
      }
      terms.push_back(std::move(right.constraint));
    }

    return BuildBalanced(&terms, 0, terms.size(), false);
  }

  ParseResult ParseAndExpr() {
    auto first = ParseUnaryExpr();
    if (!first.error.ok()) {
      return first;
    }
    std::vector<Constraint> terms;
    terms.push_back(std::move(first.constraint));

    while (Match(TokenType::kAnd)) {
      auto right = ParseUnaryExpr();
      if (!right.error.ok()) {
        return right;
      }
      terms.push_back(std::move(right.constraint));
    }

    return BuildBalanced(&terms, 0, terms.size(), true);
  }

  ParseResult ParseUnaryExpr() {
    if (Match(TokenType::kNot)) {
      // Bound the actual prefix-NOT recursion depth to keep the stack safe on a
      // long unparenthesized NOT chain (which the token pre-scan cannot see).
      if (++not_depth_ > kMaxAstDepth) {
        return {nullptr,
                {Error::Code::kConstraintError,
                 "Constraint nesting depth limit exceeded (maximum " +
                     std::to_string(kMaxAstDepth) + ")",
                 "Simplify or split the constraint expression"}};
      }
      auto child = ParseUnaryExpr();
      --not_depth_;
      if (!child.error.ok()) {
        return child;
      }
      return MakeNode(std::make_unique<NotNode>(std::move(child.constraint)));
    }
    return ParseAtom();
  }

  bool IsComparisonOp(TokenType type) const {
    return type == TokenType::kEquals || type == TokenType::kNotEquals ||
           type == TokenType::kLess || type == TokenType::kLessEqual ||
           type == TokenType::kGreater || type == TokenType::kGreaterEqual;
  }

  ParseResult ParseAtom() {
    if (Match(TokenType::kLParen)) {
      auto inner = ParseExpression();
      if (!inner.error.ok()) {
        return inner;
      }
      if (!Match(TokenType::kRParen)) {
        return {nullptr,
                {Error::Code::kConstraintError,
                 "Expected ')' at position " + std::to_string(Current().position),
                 "Mismatched parentheses"}};
      }
      return inner;
    }

    if (Current().type == TokenType::kIdentifier) {
      const Token& param_tok = Advance();

      // IN operator: ident IN { val1, val2, ... }
      if (Current().type == TokenType::kIn) {
        return ParseInExpr(param_tok);
      }

      // LIKE operator: ident LIKE pattern
      if (Current().type == TokenType::kLike) {
        return ParseLikeExpr(param_tok);
      }

      if (!IsComparisonOp(Current().type)) {
        return {nullptr,
                {Error::Code::kConstraintError,
                 "Expected operator after '" + param_tok.text + "' at position " +
                     std::to_string(Current().position),
                 "Syntax: parameter=value, parameter!=value, parameter>value, "
                 "parameter IN {values}, or parameter LIKE pattern"}};
      }
      TokenType op_type = Current().type;
      Advance();

      // Relational operators (>, >=, <, <=) with number or param
      if (op_type == TokenType::kLess || op_type == TokenType::kLessEqual ||
          op_type == TokenType::kGreater || op_type == TokenType::kGreaterEqual) {
        return ParseRelationalRhs(param_tok, op_type);
      }

      // = or != with identifier, number, or param-to-param
      if (Current().type != TokenType::kIdentifier && Current().type != TokenType::kNumber) {
        return {nullptr,
                {Error::Code::kConstraintError,
                 "Expected value after operator at position " + std::to_string(Current().position),
                 "Syntax: parameter=value or parameter!=value"}};
      }
      const Token& value_tok = Advance();
      bool is_equals = (op_type == TokenType::kEquals);

      // Resolve left parameter
      auto rp = ResolveParam(param_tok.text, params_, options_.case_sensitive);
      if (!rp.error.ok()) {
        return {nullptr, rp.error};
      }
      uint32_t left_param = rp.param_index;

      // Determine if RHS is a value of the left param or a parameter name. A
      // quoted RHS is always a literal value, never a parameter reference, so a
      // quoted token that collides with a parameter name is not silently turned
      // into a param-to-param comparison.
      bool rhs_is_value =
          IsValueOfParam(left_param, value_tok.text, params_, options_.case_sensitive);
      bool rhs_is_param = !value_tok.was_quoted &&
                          IsParameterName(value_tok.text, params_, options_.case_sensitive);

      // If it's a value of the left param, prefer param=value interpretation
      if (rhs_is_value) {
        auto rv = ResolveValue(left_param, value_tok.text, params_, options_.case_sensitive);
        if (!rv.error.ok()) {
          return {nullptr, rv.error};
        }
        if (is_equals) {
          return MakeNode(std::make_unique<EqualsNode>(left_param, rv.value_index));
        } else {
          return MakeNode(std::make_unique<NotEqualsNode>(left_param, rv.value_index));
        }
      }

      // If it's a parameter name, do param-to-param comparison
      if (rhs_is_param) {
        auto rp2 = ResolveParam(value_tok.text, params_, options_.case_sensitive);
        if (!rp2.error.ok()) {
          return {nullptr, rp2.error};
        }
        if (is_equals) {
          return MakeNode(std::make_unique<ParamEqualsNode>(
              left_param, rp2.param_index, params_[left_param].values,
              params_[rp2.param_index].values, options_.case_sensitive));
        } else {
          return MakeNode(std::make_unique<ParamNotEqualsNode>(
              left_param, rp2.param_index, params_[left_param].values,
              params_[rp2.param_index].values, options_.case_sensitive));
        }
      }

      // Neither a value nor a parameter -- error
      auto resolved =
          ResolveComparison(param_tok.text, value_tok.text, params_, options_.case_sensitive);
      return {nullptr, resolved.error};
    }

    if (Current().type == TokenType::kEnd) {
      return {nullptr,
              {Error::Code::kConstraintError, "Unexpected end of expression",
               "Expected a comparison or '('"}};
    }
    return {nullptr,
            {Error::Code::kConstraintError,
             "Unexpected token '" + Current().text + "' at position " +
                 std::to_string(Current().position),
             "Expected a comparison (e.g. param=value) or '('"}};
  }

  ParseResult ParseInExpr(const Token& param_tok) {
    Advance();  // consume IN

    auto rp = ResolveParam(param_tok.text, params_, options_.case_sensitive);
    if (!rp.error.ok()) {
      return {nullptr, rp.error};
    }
    uint32_t param_idx = rp.param_index;

    if (!Match(TokenType::kLBrace)) {
      return {nullptr,
              {Error::Code::kConstraintError,
               "Expected '{' after 'IN' at position " + std::to_string(Current().position),
               "Syntax: parameter IN {value1, value2, ...}"}};
    }

    std::vector<uint32_t> value_indices;
    // Parse first value
    if (Current().type != TokenType::kIdentifier && Current().type != TokenType::kNumber) {
      return {nullptr,
              {Error::Code::kConstraintError,
               "Expected value in set at position " + std::to_string(Current().position),
               "Syntax: parameter IN {value1, value2, ...}"}};
    }
    {
      const Token& val_tok = Advance();
      auto rv = ResolveValue(param_idx, val_tok.text, params_, options_.case_sensitive);
      if (!rv.error.ok()) {
        return {nullptr, rv.error};
      }
      value_indices.push_back(rv.value_index);
    }

    // Parse remaining values
    while (Match(TokenType::kComma)) {
      if (Current().type != TokenType::kIdentifier && Current().type != TokenType::kNumber) {
        return {nullptr,
                {Error::Code::kConstraintError,
                 "Expected value after ',' at position " + std::to_string(Current().position),
                 "Syntax: parameter IN {value1, value2, ...}"}};
      }
      const Token& val_tok = Advance();
      auto rv = ResolveValue(param_idx, val_tok.text, params_, options_.case_sensitive);
      if (!rv.error.ok()) {
        return {nullptr, rv.error};
      }
      value_indices.push_back(rv.value_index);
    }

    if (!Match(TokenType::kRBrace)) {
      return {nullptr,
              {Error::Code::kConstraintError,
               "Expected '}' at position " + std::to_string(Current().position),
               "Syntax: parameter IN {value1, value2, ...}"}};
    }

    return MakeNode(std::make_unique<InNode>(param_idx, std::move(value_indices)));
  }

  ParseResult ParseLikeExpr(const Token& param_tok) {
    Advance();  // consume LIKE

    auto rp = ResolveParam(param_tok.text, params_, options_.case_sensitive);
    if (!rp.error.ok()) {
      return {nullptr, rp.error};
    }
    uint32_t param_idx = rp.param_index;

    if (Current().type != TokenType::kIdentifier && Current().type != TokenType::kNumber) {
      return {nullptr,
              {Error::Code::kConstraintError,
               "Expected pattern after 'LIKE' at position " + std::to_string(Current().position),
               "Syntax: parameter LIKE pattern (wildcards: * = any string, "
               "? = single char)"}};
    }
    const Token& pattern_tok = Advance();

    return MakeNode(std::make_unique<LikeNode>(param_idx, pattern_tok.text,
                                               params_[param_idx].values, options_.case_sensitive));
  }

  ParseResult ParseRelationalRhs(const Token& param_tok, TokenType op_type) {
    RelOp op;
    switch (op_type) {
      case TokenType::kLess:
        op = RelOp::kLess;
        break;
      case TokenType::kLessEqual:
        op = RelOp::kLessEqual;
        break;
      case TokenType::kGreater:
        op = RelOp::kGreater;
        break;
      case TokenType::kGreaterEqual:
        op = RelOp::kGreaterEqual;
        break;
      default:
        return {
            nullptr,
            {Error::Code::kConstraintError, "Internal parser error", "Unexpected operator type"}};
    }

    auto rp = ResolveParam(param_tok.text, params_, options_.case_sensitive);
    if (!rp.error.ok()) {
      return {nullptr, rp.error};
    }
    uint32_t left_param = rp.param_index;

    if (Current().type == TokenType::kNumber) {
      const Token& num_tok = Advance();
      double literal = 0.0;
      if (!util::TryParseFiniteDouble(num_tok.text, &literal)) {
        return {nullptr,
                {Error::Code::kConstraintError,
                 "Invalid or out-of-range decimal literal '" + num_tok.text + "' at position " +
                     std::to_string(num_tok.position),
                 "Relational literals must be finite, representable decimal numbers"}};
      }
      return MakeNode(
          std::make_unique<RelationalNode>(left_param, op, literal, NumericCacheFor(left_param)));
    }

    if (Current().type == TokenType::kIdentifier) {
      const Token& rhs_tok = Advance();
      // A quoted right-hand side is a literal value, never a parameter
      // reference, so a quoted token that collides with a parameter name is not
      // silently turned into a parameter-to-parameter comparison. This is the
      // same rule = and != apply.
      if (!rhs_tok.was_quoted && IsParameterName(rhs_tok.text, params_, options_.case_sensitive)) {
        auto rp2 = ResolveParam(rhs_tok.text, params_, options_.case_sensitive);
        if (!rp2.error.ok()) {
          return {nullptr, rp2.error};
        }
        return MakeNode(std::make_unique<RelationalNode>(left_param, op, rp2.param_index,
                                                         NumericCacheFor(left_param),
                                                         NumericCacheFor(rp2.param_index)));
      }
      // Try parsing as a number (identifiers that look like numbers)
      if (IsNumeric(rhs_tok.text)) {
        double literal = 0.0;
        if (!util::TryParseFiniteDouble(rhs_tok.text, &literal)) {
          return {nullptr,
                  {Error::Code::kConstraintError,
                   "Invalid or out-of-range decimal literal '" + rhs_tok.text + "' at position " +
                       std::to_string(rhs_tok.position),
                   "Relational literals must be finite, representable decimal numbers"}};
        }
        return MakeNode(
            std::make_unique<RelationalNode>(left_param, op, literal, NumericCacheFor(left_param)));
      }
      return {nullptr,
              {Error::Code::kConstraintError,
               "Expected number or parameter after relational operator at position " +
                   std::to_string(rhs_tok.position),
               "Relational operators (>, >=, <, <=) require numeric values or "
               "parameter names"}};
    }

    return {nullptr,
            {Error::Code::kConstraintError,
             "Expected value after relational operator at position " +
                 std::to_string(Current().position),
             "Syntax: parameter > number or parameter > parameter"}};
  }

  std::vector<Token> tokens_;
  const std::vector<Parameter>& params_;
  ParseOptions options_;
  std::vector<NumericValueCachePtr> numeric_caches_;
  size_t pos_;
  size_t node_count_ = 0;
  size_t not_depth_ = 0;  ///< Current prefix-NOT recursion depth (bounds the stack).
};

}  // namespace

ParseResult ParseConstraint(const std::string& expression, const std::vector<Parameter>& params,
                            const ParseOptions& options) {
  if (expression.empty()) {
    return {nullptr,
            {Error::Code::kConstraintError, "Empty constraint expression",
             "Provide a non-empty constraint string"}};
  }
  if (expression.size() > kMaxExpressionBytes) {
    return {nullptr,
            {Error::Code::kConstraintError,
             "Constraint expression byte limit exceeded (maximum " +
                 std::to_string(kMaxExpressionBytes) + ")",
             "Simplify or split the constraint expression"}};
  }

  auto tok_result = Tokenize(expression);
  if (!tok_result.error.ok()) {
    return {nullptr, std::move(tok_result.error)};
  }

  Parser parser(std::move(tok_result.tokens), params, options);
  return parser.Parse();
}

Error AnnotateConstraintError(const std::string& expression, Error error) {
  error.message = "Invalid constraint \"" + expression + "\": " + error.message;
  return error;
}

}  // namespace model
}  // namespace coverwise
