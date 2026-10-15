/// Tokenizer for constraint expressions.

import { asciiToUpper } from '../util/string_util.js';
import { ErrorCode, type ErrorInfo, okError } from './error.js';

const MAX_TOKENS = 4096;
export const MAX_AST_DEPTH = 128;

// --- Token types ---

export enum TokenType {
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

export interface Token {
  type: TokenType;
  text: string;
  /** Unicode codepoint offset in the original expression. */
  position: number;
  /** True if this identifier came from a quoted string literal. */
  wasQuoted?: boolean;
}

// --- Tokenizer ---

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

/// Classify a word already run through asciiToUpper.
///
/// Keyword matching is case-insensitive, and it is case-insensitive by the one
/// fold the rest of the engine uses rather than a second rule of its own.
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

export function tokenize(expr: string): TokenizeResult {
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
      const type = classifyKeyword(asciiToUpper(word));
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
