/// Parser for human-readable constraint expressions.

import {
  asciiCaseInsensitiveEqual,
  asciiToUpper,
  isNumeric as isNumericString,
} from '../util/string_util.js';
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
const MAX_EXPRESSION_BYTES = 64 * 1024;
const MAX_TOKENS = 4096;
const MAX_AST_DEPTH = 128;
const MAX_AST_NODES = 1024;
const MIN_NORMAL_NUMBER = 2.2250738585072014e-308;

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
  /** Unicode codepoint offset in the original expression. */
  position: number;
  /** True if this identifier came from a quoted string literal. */
  wasQuoted?: boolean;
}

// --- Tokenizer ---

// Keyword matching is ASCII-only by design, matching the C++ core.
function toUpper(s: string): string {
  return asciiToUpper(s);
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

function buildCodepointPositions(value: string): number[] {
  const positions = new Array<number>(value.length + 1);
  let codepoints = 0;
  for (let i = 0; i < value.length; i++) {
    positions[i] = codepoints;
    const code = value.charCodeAt(i);
    if (code >= 0xd800 && code <= 0xdbff && i + 1 < value.length) {
      const next = value.charCodeAt(i + 1);
      if (next >= 0xdc00 && next <= 0xdfff) {
        positions[++i] = codepoints;
      }
    }
    codepoints++;
  }
  positions[value.length] = codepoints;
  return positions;
}

function utf8ByteLength(value: string): number {
  let bytes = 0;
  for (const character of value) {
    const codepoint = character.codePointAt(0) ?? 0;
    bytes += codepoint <= 0x7f ? 1 : codepoint <= 0x7ff ? 2 : codepoint <= 0xffff ? 3 : 4;
  }
  return bytes;
}

function isDigit(c: string): boolean {
  const code = c.charCodeAt(0);
  return code >= 0x30 && code <= 0x39;
}

function isSpace(c: string): boolean {
  return c === ' ' || c === '\t' || c === '\n' || c === '\r';
}

function isComparisonToken(type: TokenType): boolean {
  return (
    type === TokenType.Equals ||
    type === TokenType.NotEquals ||
    type === TokenType.Less ||
    type === TokenType.LessEqual ||
    type === TokenType.Greater ||
    type === TokenType.GreaterEqual
  );
}

function scanDecimal(value: string, start: number, allowSignOrLeadingDot: boolean): number {
  let i = start;
  if (allowSignOrLeadingDot && (value[i] === '+' || value[i] === '-')) {
    i++;
  }
  let digits = 0;
  while (i < value.length && isDigit(value[i])) {
    digits++;
    i++;
  }
  if (i < value.length && value[i] === '.') {
    i++;
    while (i < value.length && isDigit(value[i])) {
      digits++;
      i++;
    }
  }
  if (digits === 0) {
    return start;
  }

  if (i < value.length && (value[i] === 'e' || value[i] === 'E')) {
    const exponentStart = i;
    i++;
    if (value[i] === '+' || value[i] === '-') {
      i++;
    }
    const exponentDigitsStart = i;
    while (i < value.length && isDigit(value[i])) {
      i++;
    }
    if (i === exponentDigitsStart) {
      return exponentStart;
    }
  }
  return i;
}

function parseFiniteDecimal(value: string): number | null {
  if (!isNumericString(value)) {
    return null;
  }
  const parsed = Number(value);
  const mantissa = value.split(/[eE]/, 1)[0];
  const hasNonzeroDigit = /[1-9]/.test(mantissa);
  if (
    !Number.isFinite(parsed) ||
    (hasNonzeroDigit && (parsed === 0 || Math.abs(parsed) < MIN_NORMAL_NUMBER))
  ) {
    return null;
  }
  return parsed;
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
  const codepointPositions = buildCodepointPositions(expr);

  while (i < len) {
    if (isSpace(expr[i])) {
      i++;
      continue;
    }

    if (tokens.length >= MAX_TOKENS) {
      return {
        tokens: [],
        error: {
          code: ErrorCode.ConstraintError,
          message: `Constraint token limit exceeded (maximum ${MAX_TOKENS})`,
          detail: 'Simplify or split the constraint expression',
        },
      };
    }

    // Positions were computed once while scanning, avoiding a prefix rescan per token.
    const start = codepointPositions[i];

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

    // LIKE pattern: after the LIKE keyword, consume a glob pattern. This must be
    // checked before the number branch so a pattern that starts with a digit
    // (e.g. version LIKE 1.*) is read as a pattern rather than a decimal literal
    // that then chokes on '*'.
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

    // Number literal. Signs and a leading dot are accepted only after an operator,
    // preserving string values such as '-1' inside IN sets as identifiers.
    const afterComparison = tokens.length > 0 && isComparisonToken(tokens[tokens.length - 1].type);
    const numericStart =
      isDigit(expr[i]) ||
      (afterComparison &&
        (expr[i] === '+' ||
          expr[i] === '-' ||
          (expr[i] === '.' && i + 1 < len && isDigit(expr[i + 1]))));
    if (numericStart) {
      let j = scanDecimal(expr, i, afterComparison);
      // If followed by identifier chars, it's actually an identifier (e.g., "3d")
      if (j > i && j < len && isIdentChar(expr[j]) && expr[i] !== '+') {
        j = i;
        while (j < len && isIdentChar(expr[j])) {
          j++;
        }
        const word = expr.substring(i, j);
        tokens.push({ type: TokenType.Identifier, text: word, position: start });
      } else if (j > i) {
        const num = expr.substring(i, j);
        tokens.push({ type: TokenType.Number, text: num, position: start });
      } else if (expr[i] !== '+' && isIdentChar(expr[i])) {
        // scanDecimal found no number (e.g. a value like '-foo' after '='). Treat
        // it as an identifier value, matching how the same token is accepted
        // inside an IN set, instead of rejecting a non-numeric value that starts
        // with a sign.
        j = i;
        while (j < len && isIdentChar(expr[j])) {
          j++;
        }
        tokens.push({ type: TokenType.Identifier, text: expr.substring(i, j), position: start });
      } else {
        return {
          tokens: [],
          error: {
            code: ErrorCode.ConstraintError,
            message: `Invalid decimal literal at position ${start}`,
            detail: 'Expected a finite decimal number',
          },
        };
      }
      i = j;
      expectPattern = false;
      continue;
    }

    // Quoted string: "..." or '...'. Supports backslash escapes \" and \\ so a
    // quoted value can contain its own quote character or a literal backslash.
    if (expr[i] === '"' || expr[i] === "'") {
      const quote = expr[i];
      let j = i + 1;
      let content = '';
      let terminated = false;
      while (j < len) {
        const c = expr[j];
        if (c === '\\' && j + 1 < len && (expr[j + 1] === quote || expr[j + 1] === '\\')) {
          content += expr[j + 1];
          j += 2;
          continue;
        }
        if (c === quote) {
          terminated = true;
          break;
        }
        content += c;
        j++;
      }
      if (!terminated) {
        return {
          tokens: [],
          error: {
            code: ErrorCode.ConstraintError,
            message: `Unterminated string literal starting at position ${start}`,
            detail: '',
          },
        };
      }
      tokens.push({ type: TokenType.Identifier, text: content, position: start, wasQuoted: true });
      i = j + 1;
      expectPattern = false;
      continue;
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

  // Bound only the actual nesting depth (parenthesis nesting), not the total
  // count of operators. A flat expression such as 200 OR-connected clauses has
  // shallow nesting and must be accepted; deep NOT recursion is bounded inside
  // the recursive-descent parser itself (see parseUnary).
  let parenDepth = 0;
  for (const token of tokens) {
    if (token.type === TokenType.LParen) {
      parenDepth++;
      if (parenDepth > MAX_AST_DEPTH) {
        return {
          tokens: [],
          error: {
            code: ErrorCode.ConstraintError,
            message: `Constraint nesting depth limit exceeded (maximum ${MAX_AST_DEPTH})`,
            detail: 'Simplify or split the constraint expression',
          },
        };
      }
    } else if (token.type === TokenType.RParen && parenDepth > 0) {
      parenDepth--;
    }
  }

  tokens.push({ type: TokenType.End, text: '', position: codepointPositions[len] });
  return { tokens, error: okError() };
}

// --- Name resolution ---

function namesEqual(a: string, b: string, caseSensitive: boolean): boolean {
  if (caseSensitive) {
    return a === b;
  }
  // Case-insensitive name resolution is ASCII-only, matching the C++ core.
  return asciiCaseInsensitiveEqual(a, b);
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

// --- Recursive descent parser ---

class Parser {
  private pos: number;
  private nodeCount = 0;
  /** Current prefix-NOT recursion depth (bounds the stack). */
  private notDepth = 0;

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

  private makeNode(constraint: ConstraintNode): ParseResult {
    this.nodeCount++;
    if (this.nodeCount > MAX_AST_NODES) {
      return {
        constraint: null,
        error: {
          code: ErrorCode.ConstraintError,
          message: `Constraint AST node limit exceeded (maximum ${MAX_AST_NODES})`,
          detail: 'Simplify or split the constraint expression',
        },
      };
    }
    return { constraint, error: okError() };
  }

  private buildBalanced(
    terms: ConstraintNode[],
    begin: number,
    end: number,
    isAnd: boolean,
  ): ParseResult {
    if (end - begin === 1) {
      return { constraint: terms[begin], error: okError() };
    }
    const middle = begin + Math.floor((end - begin) / 2);
    const left = this.buildBalanced(terms, begin, middle, isAnd);
    if (left.error.code !== ErrorCode.Ok || left.constraint === null) {
      return left;
    }
    const right = this.buildBalanced(terms, middle, end, isAnd);
    if (right.error.code !== ErrorCode.Ok || right.constraint === null) {
      return right;
    }
    return this.makeNode(
      isAnd
        ? new AndNode(left.constraint, right.constraint)
        : new OrNode(left.constraint, right.constraint),
    );
  }

  private parseExpression(): ParseResult {
    return this.parseImpliesExpr();
  }

  private parseImpliesExpr(): ParseResult {
    if (this.current().type === TokenType.If) {
      this.advance();
      const antecedent = this.parseOrExpr();
      if (antecedent.error.code !== ErrorCode.Ok || antecedent.constraint === null) {
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
      if (consequent.error.code !== ErrorCode.Ok || consequent.constraint === null) {
        return consequent;
      }

      // Check for optional ELSE clause
      if (this.current().type === TokenType.Else) {
        this.advance();
        const elseBranch = this.parseOrExpr();
        if (elseBranch.error.code !== ErrorCode.Ok || elseBranch.constraint === null) {
          return elseBranch;
        }
        return this.makeNode(
          new IfThenElseNode(antecedent.constraint, consequent.constraint, elseBranch.constraint),
        );
      }

      return this.makeNode(new ImpliesNode(antecedent.constraint, consequent.constraint));
    }

    const left = this.parseOrExpr();
    if (left.error.code !== ErrorCode.Ok || left.constraint === null) {
      return left;
    }

    if (this.match(TokenType.Implies)) {
      const right = this.parseOrExpr();
      if (right.error.code !== ErrorCode.Ok || right.constraint === null) {
        return right;
      }
      return this.makeNode(new ImpliesNode(left.constraint, right.constraint));
    }

    return left;
  }

  private parseOrExpr(): ParseResult {
    const first = this.parseAndExpr();
    if (first.error.code !== ErrorCode.Ok || first.constraint === null) {
      return first;
    }
    const terms: ConstraintNode[] = [first.constraint];

    while (this.match(TokenType.Or)) {
      const right = this.parseAndExpr();
      if (right.error.code !== ErrorCode.Ok) {
        return right;
      }
      if (right.constraint === null) {
        return right;
      }
      terms.push(right.constraint);
    }

    return this.buildBalanced(terms, 0, terms.length, false);
  }

  private parseAndExpr(): ParseResult {
    const first = this.parseUnaryExpr();
    if (first.error.code !== ErrorCode.Ok || first.constraint === null) {
      return first;
    }
    const terms: ConstraintNode[] = [first.constraint];

    while (this.match(TokenType.And)) {
      const right = this.parseUnaryExpr();
      if (right.error.code !== ErrorCode.Ok) {
        return right;
      }
      if (right.constraint === null) {
        return right;
      }
      terms.push(right.constraint);
    }

    return this.buildBalanced(terms, 0, terms.length, true);
  }

  private parseUnaryExpr(): ParseResult {
    if (this.match(TokenType.Not)) {
      // Bound the actual prefix-NOT recursion depth to keep the stack safe on a
      // long unparenthesized NOT chain (which the token pre-scan cannot see).
      if (++this.notDepth > MAX_AST_DEPTH) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Constraint nesting depth limit exceeded (maximum ${MAX_AST_DEPTH})`,
            detail: 'Simplify or split the constraint expression',
          },
        };
      }
      const child = this.parseUnaryExpr();
      this.notDepth--;
      if (child.error.code !== ErrorCode.Ok || child.constraint === null) {
        return child;
      }
      return this.makeNode(new NotNode(child.constraint));
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

      // Determine if RHS is a value of the left param or a parameter name. A
      // quoted RHS is always a literal value, never a parameter reference, so a
      // quoted token that collides with a parameter name is not silently turned
      // into a param-to-param comparison.
      const rhsIsValue = isValueOfParam(
        leftParam,
        valueTok.text,
        this.params,
        this.options.caseSensitive,
      );
      const rhsIsParam =
        !valueTok.wasQuoted &&
        isParameterName(valueTok.text, this.params, this.options.caseSensitive);

      // If it's a value of the left param, prefer param=value interpretation
      if (rhsIsValue) {
        const rv = resolveValue(leftParam, valueTok.text, this.params, this.options.caseSensitive);
        if (rv.error.code !== ErrorCode.Ok) {
          return { constraint: null, error: rv.error };
        }
        if (isEquals) {
          return this.makeNode(new EqualsNode(leftParam, rv.valueIndex));
        }
        return this.makeNode(new NotEqualsNode(leftParam, rv.valueIndex));
      }

      // If it's a parameter name, do param-to-param comparison
      if (rhsIsParam) {
        const rp2 = resolveParam(valueTok.text, this.params, this.options.caseSensitive);
        if (rp2.error.code !== ErrorCode.Ok) {
          return { constraint: null, error: rp2.error };
        }
        if (isEquals) {
          return this.makeNode(
            new ParamEqualsNode(
              leftParam,
              rp2.paramIndex,
              this.params[leftParam].values,
              this.params[rp2.paramIndex].values,
              this.options.caseSensitive,
            ),
          );
        }
        return this.makeNode(
          new ParamNotEqualsNode(
            leftParam,
            rp2.paramIndex,
            this.params[leftParam].values,
            this.params[rp2.paramIndex].values,
            this.options.caseSensitive,
          ),
        );
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

    return this.makeNode(new InNode(paramIdx, valueIndices));
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

    return this.makeNode(new LikeNode(paramIdx, patternTok.text, this.params[paramIdx].values));
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
      const literal = parseFiniteDecimal(numTok.text);
      if (literal === null) {
        return {
          constraint: null,
          error: {
            code: ErrorCode.ConstraintError,
            message: `Invalid or out-of-range decimal literal '${numTok.text}' at position ${numTok.position}`,
            detail: 'Relational literals must be finite, representable decimal numbers',
          },
        };
      }
      return this.makeNode(
        RelationalNode.fromLiteral(leftParam, op, literal, this.params[leftParam].values),
      );
    }

    if (this.current().type === TokenType.Identifier) {
      const rhsTok = this.advance();
      // Check if RHS is a parameter name
      if (isParameterName(rhsTok.text, this.params, this.options.caseSensitive)) {
        const rp2 = resolveParam(rhsTok.text, this.params, this.options.caseSensitive);
        if (rp2.error.code !== ErrorCode.Ok) {
          return { constraint: null, error: rp2.error };
        }
        return this.makeNode(
          RelationalNode.fromParams(
            leftParam,
            op,
            rp2.paramIndex,
            this.params[leftParam].values,
            this.params[rp2.paramIndex].values,
          ),
        );
      }
      // Try parsing as a number
      if (isNumericString(rhsTok.text)) {
        const literal = parseFiniteDecimal(rhsTok.text);
        if (literal === null) {
          return {
            constraint: null,
            error: {
              code: ErrorCode.ConstraintError,
              message: `Invalid or out-of-range decimal literal '${rhsTok.text}' at position ${rhsTok.position}`,
              detail: 'Relational literals must be finite, representable decimal numbers',
            },
          };
        }
        return this.makeNode(
          RelationalNode.fromLiteral(leftParam, op, literal, this.params[leftParam].values),
        );
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
/**
 * Prefix a constraint parse error with the offending expression.
 *
 * Produces the uniform cross-surface message
 * `Invalid constraint "<expression>": <original message>` so every surface
 * (CLI, WASM, pure-JS generate and analyze) reports constraint failures in a
 * single format that names the source expression. The detail field is left
 * unchanged; each surface appends it consistently.
 */
export function annotateConstraintError(expression: string, error: ErrorInfo): ErrorInfo {
  return { ...error, message: `Invalid constraint "${expression}": ${error.message}` };
}

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
  if (utf8ByteLength(expression) > MAX_EXPRESSION_BYTES) {
    return {
      constraint: null,
      error: {
        code: ErrorCode.ConstraintError,
        message: `Constraint expression byte limit exceeded (maximum ${MAX_EXPRESSION_BYTES})`,
        detail: 'Simplify or split the constraint expression',
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
