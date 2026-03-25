/// Parser for human-readable constraint expressions.

import {
  AndNode,
  type ConstraintNode,
  EqualsNode,
  IfThenElseNode,
  ImpliesNode,
  InNode,
  LikeNode,
  NotEqualsNode,
  NotNode,
  OrNode,
  ParamEqualsNode,
  ParamNotEqualsNode,
  RelationalNode,
  RelOp,
} from './constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from './error.js';

/** Minimal parameter interface required by the constraint parser. */
export interface Parameter {
  name: string;
  values: string[];
  /** Find a value index by name, checking both primary values and aliases. */
  findValueIndex(name: string, caseSensitive: boolean): number;
}

/** Result of parsing a constraint expression. */
export interface ParseResult {
  constraint: ConstraintNode | null;
  error: ErrorInfo;
}

/** Options controlling constraint parsing behavior. */
export interface ParseOptions {
  /**
   * When false (default), parameter and value name resolution
   * is case-insensitive. When true, exact match is required.
   */
  caseSensitive: boolean;
}

const NOT_FOUND = 0xffffffff;

// --- Token types ---

enum TokenType {
  Identifier,
  Number,
  Equals,
  NotEquals,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LParen,
  RParen,
  LBrace,
  RBrace,
  Comma,
  And,
  Or,
  Not,
  If,
  Then,
  Else,
  Implies,
  In,
  Like,
  End,
}

interface Token {
  type: TokenType;
  text: string;
  position: number;
}

// --- Tokenizer ---

function toUpper(s: string): string {
  return s.toUpperCase();
}

function isIdentChar(c: string): boolean {
  const code = c.charCodeAt(0);
  return (
    (code >= 0x30 && code <= 0x39) || // 0-9
    (code >= 0x41 && code <= 0x5a) || // A-Z
    (code >= 0x61 && code <= 0x7a) || // a-z
    c === '_' ||
    c === '-' ||
    c === '.' ||
    code >= 0x80
  );
}

function isGlobPatternChar(c: string): boolean {
  return isIdentChar(c) || c === '*' || c === '?' || c === '.';
}

function isDigit(c: string): boolean {
  const code = c.charCodeAt(0);
  return code >= 0x30 && code <= 0x39;
}

function isSpace(c: string): boolean {
  return c === ' ' || c === '\t' || c === '\n' || c === '\r';
}

function classifyKeyword(upper: string): TokenType {
  switch (upper) {
    case 'AND':
      return TokenType.And;
    case 'OR':
      return TokenType.Or;
    case 'NOT':
      return TokenType.Not;
    case 'IF':
      return TokenType.If;
    case 'THEN':
      return TokenType.Then;
    case 'ELSE':
      return TokenType.Else;
    case 'IMPLIES':
      return TokenType.Implies;
    case 'IN':
      return TokenType.In;
    case 'LIKE':
      return TokenType.Like;
    default:
      return TokenType.Identifier;
  }
}

interface TokenizeResult {
  tokens: Token[];
  error: ErrorInfo;
}

function tokenize(expr: string): TokenizeResult {
  const tokens: Token[] = [];
  let i = 0;
  const len = expr.length;
  let expectPattern = false;

  while (i < len) {
    if (isSpace(expr[i])) {
      i++;
      continue;
    }

    const start = i;

    if (expr[i] === '(') {
      tokens.push({ type: TokenType.LParen, text: '(', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === ')') {
      tokens.push({ type: TokenType.RParen, text: ')', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '{') {
      tokens.push({ type: TokenType.LBrace, text: '{', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '}') {
      tokens.push({ type: TokenType.RBrace, text: '}', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === ',') {
      tokens.push({ type: TokenType.Comma, text: ',', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '!' && i + 1 < len && expr[i + 1] === '=') {
      tokens.push({ type: TokenType.NotEquals, text: '!=', position: start });
      i += 2;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '<' && i + 1 < len && expr[i + 1] === '=') {
      tokens.push({ type: TokenType.LessEqual, text: '<=', position: start });
      i += 2;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '>' && i + 1 < len && expr[i + 1] === '=') {
      tokens.push({ type: TokenType.GreaterEqual, text: '>=', position: start });
      i += 2;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '<') {
      tokens.push({ type: TokenType.Less, text: '<', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '>') {
      tokens.push({ type: TokenType.Greater, text: '>', position: start });
      i++;
      expectPattern = false;
      continue;
    }
    if (expr[i] === '=') {
      tokens.push({ type: TokenType.Equals, text: '=', position: start });
      i++;
      expectPattern = false;
      continue;
    }

    // Negative number: '-' followed by digit, only if preceded by an operator
    if (expr[i] === '-' && i + 1 < len && isDigit(expr[i + 1])) {
      let isNegativeNum = tokens.length === 0;
      if (!isNegativeNum && tokens.length > 0) {
        const prev = tokens[tokens.length - 1].type;
        isNegativeNum =
          prev === TokenType.Equals ||
          prev === TokenType.NotEquals ||
          prev === TokenType.Less ||
          prev === TokenType.LessEqual ||
          prev === TokenType.Greater ||
          prev === TokenType.GreaterEqual;
      }
      if (isNegativeNum) {
        let j = i + 1;
        while (j < len && (isDigit(expr[j]) || expr[j] === '.')) {
          j++;
        }
        const num = expr.substring(i, j);
        tokens.push({ type: TokenType.Number, text: num, position: start });
        i = j;
        expectPattern = false;
        continue;
      }
    }

    // Number literal
    if (isDigit(expr[i])) {
      let j = i;
      while (j < len && (isDigit(expr[j]) || expr[j] === '.')) {
        j++;
      }
      // If followed by identifier chars, it's actually an identifier (e.g., "3d")
      if (j < len && isIdentChar(expr[j]) && expr[j] !== '-') {
        while (j < len && isIdentChar(expr[j])) {
          j++;
        }
        const word = expr.substring(i, j);
        tokens.push({ type: TokenType.Identifier, text: word, position: start });
      } else {
        const num = expr.substring(i, j);
        tokens.push({ type: TokenType.Number, text: num, position: start });
      }
      i = j;
      expectPattern = false;
      continue;
    }

    // LIKE pattern: after LIKE keyword, consume pattern with glob chars
    if (expectPattern) {
      let j = i;
      while (j < len && isGlobPatternChar(expr[j])) {
        j++;
      }
      if (j > i) {
        const pattern = expr.substring(i, j);
        tokens.push({ type: TokenType.Identifier, text: pattern, position: start });
        i = j;
        expectPattern = false;
        continue;
      }
    }

    if (isIdentChar(expr[i])) {
      let j = i;
      while (j < len && isIdentChar(expr[j])) {
        j++;
      }
      const word = expr.substring(i, j);
      const upper = toUpper(word);

      const type = classifyKeyword(upper);
      tokens.push({ type, text: word, position: start });
      i = j;
      expectPattern = type === TokenType.Like;
      continue;
    }

    // Handle glob pattern chars at top level
    if (expr[i] === '*' || expr[i] === '?') {
      let j = i;
      while (j < len && isGlobPatternChar(expr[j])) {
        j++;
      }
      const pattern = expr.substring(i, j);
      tokens.push({ type: TokenType.Identifier, text: pattern, position: start });
      i = j;
      expectPattern = false;
      continue;
    }

    return {
      tokens: [],
      error: {
        code: ErrorCode.ConstraintError,
        message: `Unexpected character '${expr[i]}' at position ${start}`,
        detail: '',
      },
    };
  }

  tokens.push({ type: TokenType.End, text: '', position: len });
  return { tokens, error: okError() };
}

// --- Name resolution ---

function namesEqual(a: string, b: string, caseSensitive: boolean): boolean {
  if (caseSensitive) {
    return a === b;
  }
  return a.toLowerCase() === b.toLowerCase();
}

interface ResolvedParam {
  paramIndex: number;
  error: ErrorInfo;
}

function resolveParam(
  paramName: string,
  params: Parameter[],
  caseSensitive: boolean,
): ResolvedParam {
  for (let i = 0; i < params.length; i++) {
    if (namesEqual(params[i].name, paramName, caseSensitive)) {
      return { paramIndex: i, error: okError() };
    }
  }
  const available = params.map((p) => p.name).join(', ');
  return {
    paramIndex: 0,
    error: {
      code: ErrorCode.ConstraintError,
      message: `Unknown parameter '${paramName}'`,
      detail: `Available parameters: ${available}`,
    },
  };
}

interface ResolvedComparison {
  paramIndex: number;
  valueIndex: number;
  error: ErrorInfo;
}

function resolveComparison(
  paramName: string,
  valueName: string,
  params: Parameter[],
  caseSensitive: boolean,
): ResolvedComparison {
  const rp = resolveParam(paramName, params, caseSensitive);
  if (rp.error.code !== ErrorCode.Ok) {
    return { paramIndex: 0, valueIndex: 0, error: rp.error };
  }
  const paramIdx = rp.paramIndex;

  const valIdx = params[paramIdx].findValueIndex(valueName, caseSensitive);
  if (valIdx === NOT_FOUND) {
    const available = params[paramIdx].values.join(', ');
    return {
      paramIndex: 0,
      valueIndex: 0,
      error: {
        code: ErrorCode.ConstraintError,
        message: `Unknown value '${valueName}' for parameter '${paramName}'`,
        detail: `Available values: ${available}`,
      },
    };
  }

  return { paramIndex: paramIdx, valueIndex: valIdx, error: okError() };
}

interface ResolvedValue {
  valueIndex: number;
  error: ErrorInfo;
}

function resolveValue(
  paramIndex: number,
  valueName: string,
  params: Parameter[],
  caseSensitive: boolean,
): ResolvedValue {
  const idx = params[paramIndex].findValueIndex(valueName, caseSensitive);
  if (idx !== NOT_FOUND) {
    return { valueIndex: idx, error: okError() };
  }
  const available = params[paramIndex].values.join(', ');
  return {
    valueIndex: 0,
    error: {
      code: ErrorCode.ConstraintError,
      message: `Unknown value '${valueName}' for parameter '${params[paramIndex].name}'`,
      detail: `Available values: ${available}`,
    },
  };
}

function isParameterName(name: string, params: Parameter[], caseSensitive: boolean): boolean {
  for (const p of params) {
    if (namesEqual(p.name, name, caseSensitive)) {
      return true;
    }
  }
  return false;
}

function isValueOfParam(
  paramIndex: number,
  name: string,
  params: Parameter[],
  caseSensitive: boolean,
): boolean {
  return params[paramIndex].findValueIndex(name, caseSensitive) !== NOT_FOUND;
}

function isNumericString(s: string): boolean {
  if (s.length === 0) {
    return false;
  }
  const n = Number(s);
  return !Number.isNaN(n) && Number.isFinite(n);
}

// --- Recursive descent parser ---

class Parser {
  private pos: number;

  constructor(
    private readonly tokens: Token[],
    private readonly params: Parameter[],
    private readonly options: ParseOptions,
  ) {
    this.pos = 0;
  }

  parse(): ParseResult {
    const result = this.parseExpression();
    if (result.error.code !== ErrorCode.Ok) {
      return result;
    }
    if (this.current().type !== TokenType.End) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Unexpected token '${this.current().text}' at position ${this.current().position}`,
          detail: 'Expected end of expression',
        },
      };
    }
    return result;
  }

  private current(): Token {
    return this.tokens[this.pos];
  }

  private advance(): Token {
    const tok = this.tokens[this.pos];
    if (this.pos + 1 < this.tokens.length) {
      this.pos++;
    }
    return tok;
  }

  private match(type: TokenType): boolean {
    if (this.current().type === type) {
      this.advance();
      return true;
    }
    return false;
  }

  private parseExpression(): ParseResult {
    return this.parseImpliesExpr();
  }

  private parseImpliesExpr(): ParseResult {
    if (this.current().type === TokenType.If) {
      this.advance();
      const antecedent = this.parseOrExpr();
      if (antecedent.error.code !== ErrorCode.Ok) {
        return antecedent;
      }
      if (this.current().type !== TokenType.Then) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Expected 'THEN' after 'IF' clause at position ${this.current().position}`,
            detail: 'Syntax: IF <condition> THEN <condition>',
          },
        };
      }
      this.advance();
      const consequent = this.parseOrExpr();
      if (consequent.error.code !== ErrorCode.Ok) {
        return consequent;
      }

      // Check for optional ELSE clause
      if (this.current().type === TokenType.Else) {
        this.advance();
        const elseBranch = this.parseOrExpr();
        if (elseBranch.error.code !== ErrorCode.Ok) {
          return elseBranch;
        }
        return {
          constraint: new IfThenElseNode(
            antecedent.constraint!,
            consequent.constraint!,
            elseBranch.constraint!,
          ),
          error: okError(),
        };
      }

      return {
        constraint: new ImpliesNode(antecedent.constraint!, consequent.constraint!),
        error: okError(),
      };
    }

    const left = this.parseOrExpr();
    if (left.error.code !== ErrorCode.Ok) {
      return left;
    }

    if (this.match(TokenType.Implies)) {
      const right = this.parseOrExpr();
      if (right.error.code !== ErrorCode.Ok) {
        return right;
      }
      return {
        constraint: new ImpliesNode(left.constraint!, right.constraint!),
        error: okError(),
      };
    }

    return left;
  }

  private parseOrExpr(): ParseResult {
    let left = this.parseAndExpr();
    if (left.error.code !== ErrorCode.Ok) {
      return left;
    }

    while (this.match(TokenType.Or)) {
      const right = this.parseAndExpr();
      if (right.error.code !== ErrorCode.Ok) {
        return right;
      }
      left = {
        constraint: new OrNode(left.constraint!, right.constraint!),
        error: okError(),
      };
    }

    return left;
  }

  private parseAndExpr(): ParseResult {
    let left = this.parseUnaryExpr();
    if (left.error.code !== ErrorCode.Ok) {
      return left;
    }

    while (this.match(TokenType.And)) {
      const right = this.parseUnaryExpr();
      if (right.error.code !== ErrorCode.Ok) {
        return right;
      }
      left = {
        constraint: new AndNode(left.constraint!, right.constraint!),
        error: okError(),
      };
    }

    return left;
  }

  private parseUnaryExpr(): ParseResult {
    if (this.match(TokenType.Not)) {
      const child = this.parseUnaryExpr();
      if (child.error.code !== ErrorCode.Ok) {
        return child;
      }
      return {
        constraint: new NotNode(child.constraint!),
        error: okError(),
      };
    }
    return this.parseAtom();
  }

  private isComparisonOp(type: TokenType): boolean {
    return (
      type === TokenType.Equals ||
      type === TokenType.NotEquals ||
      type === TokenType.Less ||
      type === TokenType.LessEqual ||
      type === TokenType.Greater ||
      type === TokenType.GreaterEqual
    );
  }

  private parseAtom(): ParseResult {
    if (this.match(TokenType.LParen)) {
      const inner = this.parseExpression();
      if (inner.error.code !== ErrorCode.Ok) {
        return inner;
      }
      if (!this.match(TokenType.RParen)) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Expected ')' at position ${this.current().position}`,
            detail: 'Mismatched parentheses',
          },
        };
      }
      return inner;
    }

    if (this.current().type === TokenType.Identifier) {
      const paramTok = this.advance();

      // IN operator: ident IN { val1, val2, ... }
      if (this.current().type === TokenType.In) {
        return this.parseInExpr(paramTok);
      }

      // LIKE operator: ident LIKE pattern
      if (this.current().type === TokenType.Like) {
        return this.parseLikeExpr(paramTok);
      }

      if (!this.isComparisonOp(this.current().type)) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Expected operator after '${paramTok.text}' at position ${this.current().position}`,
            detail:
              'Syntax: parameter=value, parameter!=value, parameter>value, ' +
              'parameter IN {values}, or parameter LIKE pattern',
          },
        };
      }
      const opType = this.current().type;
      this.advance();

      // Relational operators (>, >=, <, <=) with number or param
      if (
        opType === TokenType.Less ||
        opType === TokenType.LessEqual ||
        opType === TokenType.Greater ||
        opType === TokenType.GreaterEqual
      ) {
        return this.parseRelationalRhs(paramTok, opType);
      }

      // = or != with identifier, number, or param-to-param
      if (
        this.current().type !== TokenType.Identifier &&
        this.current().type !== TokenType.Number
      ) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Expected value after operator at position ${this.current().position}`,
            detail: 'Syntax: parameter=value or parameter!=value',
          },
        };
      }
      const valueTok = this.advance();
      const isEquals = opType === TokenType.Equals;

      // Resolve left parameter
      const rp = resolveParam(paramTok.text, this.params, this.options.caseSensitive);
      if (rp.error.code !== ErrorCode.Ok) {
        return { constraint: null, error: rp.error };
      }
      const leftParam = rp.paramIndex;

      // Determine if RHS is a value of the left param or a parameter name
      const rhsIsValue = isValueOfParam(
        leftParam,
        valueTok.text,
        this.params,
        this.options.caseSensitive,
      );
      const rhsIsParam = isParameterName(valueTok.text, this.params, this.options.caseSensitive);

      // If it's a value of the left param, prefer param=value interpretation
      if (rhsIsValue) {
        const rv = resolveValue(leftParam, valueTok.text, this.params, this.options.caseSensitive);
        if (rv.error.code !== ErrorCode.Ok) {
          return { constraint: null, error: rv.error };
        }
        if (isEquals) {
          return {
            constraint: new EqualsNode(leftParam, rv.valueIndex),
            error: okError(),
          };
        }
        return {
          constraint: new NotEqualsNode(leftParam, rv.valueIndex),
          error: okError(),
        };
      }

      // If it's a parameter name, do param-to-param comparison
      if (rhsIsParam) {
        const rp2 = resolveParam(valueTok.text, this.params, this.options.caseSensitive);
        if (rp2.error.code !== ErrorCode.Ok) {
          return { constraint: null, error: rp2.error };
        }
        if (isEquals) {
          return {
            constraint: new ParamEqualsNode(
              leftParam,
              rp2.paramIndex,
              this.params[leftParam].values,
              this.params[rp2.paramIndex].values,
            ),
            error: okError(),
          };
        }
        return {
          constraint: new ParamNotEqualsNode(
            leftParam,
            rp2.paramIndex,
            this.params[leftParam].values,
            this.params[rp2.paramIndex].values,
          ),
          error: okError(),
        };
      }

      // Neither a value nor a parameter -- error
      const resolved = resolveComparison(
        paramTok.text,
        valueTok.text,
        this.params,
        this.options.caseSensitive,
      );
      return { constraint: null, error: resolved.error };
    }

    if (this.current().type === TokenType.End) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: 'Unexpected end of expression',
          detail: "Expected a comparison or '('",
        },
      };
    }
    return {
      constraint: null,
      error: {
        code: ErrorCode.ConstraintError,
        message: `Unexpected token '${this.current().text}' at position ${this.current().position}`,
        detail: "Expected a comparison (e.g. param=value) or '('",
      },
    };
  }

  private parseInExpr(paramTok: Token): ParseResult {
    this.advance(); // consume IN

    const rp = resolveParam(paramTok.text, this.params, this.options.caseSensitive);
    if (rp.error.code !== ErrorCode.Ok) {
      return { constraint: null, error: rp.error };
    }
    const paramIdx = rp.paramIndex;

    if (!this.match(TokenType.LBrace)) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Expected '{' after 'IN' at position ${this.current().position}`,
          detail: 'Syntax: parameter IN {value1, value2, ...}',
        },
      };
    }

    const valueIndices: number[] = [];
    // Parse first value
    if (this.current().type !== TokenType.Identifier && this.current().type !== TokenType.Number) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Expected value in set at position ${this.current().position}`,
          detail: 'Syntax: parameter IN {value1, value2, ...}',
        },
      };
    }
    {
      const valTok = this.advance();
      const rv = resolveValue(paramIdx, valTok.text, this.params, this.options.caseSensitive);
      if (rv.error.code !== ErrorCode.Ok) {
        return { constraint: null, error: rv.error };
      }
      valueIndices.push(rv.valueIndex);
    }

    // Parse remaining values
    while (this.match(TokenType.Comma)) {
      if (
        this.current().type !== TokenType.Identifier &&
        this.current().type !== TokenType.Number
      ) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Expected value after ',' at position ${this.current().position}`,
            detail: 'Syntax: parameter IN {value1, value2, ...}',
          },
        };
      }
      const valTok = this.advance();
      const rv = resolveValue(paramIdx, valTok.text, this.params, this.options.caseSensitive);
      if (rv.error.code !== ErrorCode.Ok) {
        return { constraint: null, error: rv.error };
      }
      valueIndices.push(rv.valueIndex);
    }

    if (!this.match(TokenType.RBrace)) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Expected '}' at position ${this.current().position}`,
          detail: 'Syntax: parameter IN {value1, value2, ...}',
        },
      };
    }

    return {
      constraint: new InNode(paramIdx, valueIndices),
      error: okError(),
    };
  }

  private parseLikeExpr(paramTok: Token): ParseResult {
    this.advance(); // consume LIKE

    const rp = resolveParam(paramTok.text, this.params, this.options.caseSensitive);
    if (rp.error.code !== ErrorCode.Ok) {
      return { constraint: null, error: rp.error };
    }
    const paramIdx = rp.paramIndex;

    if (this.current().type !== TokenType.Identifier && this.current().type !== TokenType.Number) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Expected pattern after 'LIKE' at position ${this.current().position}`,
          detail: 'Syntax: parameter LIKE pattern (wildcards: * = any string, ? = single char)',
        },
      };
    }
    const patternTok = this.advance();

    return {
      constraint: new LikeNode(paramIdx, patternTok.text, this.params[paramIdx].values),
      error: okError(),
    };
  }

  private parseRelationalRhs(paramTok: Token, opType: TokenType): ParseResult {
    let op: RelOp;
    switch (opType) {
      case TokenType.Less:
        op = RelOp.Less;
        break;
      case TokenType.LessEqual:
        op = RelOp.LessEqual;
        break;
      case TokenType.Greater:
        op = RelOp.Greater;
        break;
      case TokenType.GreaterEqual:
        op = RelOp.GreaterEqual;
        break;
      default:
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: 'Internal parser error',
            detail: 'Unexpected operator type',
          },
        };
    }

    const rp = resolveParam(paramTok.text, this.params, this.options.caseSensitive);
    if (rp.error.code !== ErrorCode.Ok) {
      return { constraint: null, error: rp.error };
    }
    const leftParam = rp.paramIndex;

    if (this.current().type === TokenType.Number) {
      const numTok = this.advance();
      const literal = Number.parseFloat(numTok.text);
      return {
        constraint: RelationalNode.fromLiteral(
          leftParam,
          op,
          literal,
          this.params[leftParam].values,
        ),
        error: okError(),
      };
    }

    if (this.current().type === TokenType.Identifier) {
      const rhsTok = this.advance();
      // Check if RHS is a parameter name
      if (isParameterName(rhsTok.text, this.params, this.options.caseSensitive)) {
        const rp2 = resolveParam(rhsTok.text, this.params, this.options.caseSensitive);
        if (rp2.error.code !== ErrorCode.Ok) {
          return { constraint: null, error: rp2.error };
        }
        return {
          constraint: RelationalNode.fromParams(
            leftParam,
            op,
            rp2.paramIndex,
            this.params[leftParam].values,
            this.params[rp2.paramIndex].values,
          ),
          error: okError(),
        };
      }
      // Try parsing as a number
      if (isNumericString(rhsTok.text)) {
        const literal = Number.parseFloat(rhsTok.text);
        return {
          constraint: RelationalNode.fromLiteral(
            leftParam,
            op,
            literal,
            this.params[leftParam].values,
          ),
          error: okError(),
        };
      }
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Expected number or parameter after relational operator at position ${rhsTok.position}`,
          detail: 'Relational operators (>, >=, <, <=) require numeric values or parameter names',
        },
      };
    }

    return {
      constraint: null,
      error: {
        code: ErrorCode.ConstraintError,
        message: `Expected value after relational operator at position ${this.current().position}`,
        detail: 'Syntax: parameter > number or parameter > parameter',
      },
    };
  }
}

/**
 * Parse a human-readable constraint expression into an AST.
 *
 * Supported syntax examples:
 *   "IF os=mac THEN browser!=ie"
 *   "IF os=mac THEN browser!=ie ELSE arch!=arm"
 *   "NOT (os=win AND browser=safari)"
 *   "os=linux IMPLIES arch!=arm"
 *   "os=win OR browser=chrome"
 *   "NOT os=linux"
 *   "version > 3"
 *   "env IN {staging, prod}"
 *   "browser LIKE chrome*"
 *   "start_date < end_date"   (parameter-to-parameter comparison)
 *
 * Keywords (case-insensitive): IF, THEN, ELSE, IMPLIES, AND, OR, NOT, IN, LIKE
 * Operators: = != > >= < <=
 * Parentheses: ( )
 * Set literals: { value1, value2, ... }
 *
 * @param expression The constraint string to parse.
 * @param params The parameter definitions (used to resolve names to indices).
 * @param options Parsing options (e.g., case sensitivity). Defaults to case-insensitive.
 * @returns ParseResult with the AST on success, or an error on failure.
 */
export function parseConstraint(
  expression: string,
  params: Parameter[],
  options: ParseOptions = { caseSensitive: false },
): ParseResult {
  if (expression.length === 0) {
    return {
      constraint: null,
      error: {
        code: ErrorCode.ConstraintError,
        message: 'Empty constraint expression',
        detail: 'Provide a non-empty constraint string',
      },
    };
  }

  const tokResult = tokenize(expression);
  if (tokResult.error.code !== ErrorCode.Ok) {
    return { constraint: null, error: tokResult.error };
  }

  const parser = new Parser(tokResult.tokens, params, options);
  return parser.parse();
}
