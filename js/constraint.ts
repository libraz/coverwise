/**
 * Fluent constraint builder for coverwise.
 *
 * Generates constraint strings compatible with the C++ constraint parser.
 *
 * @example
 * import { when, not, allOf, anyOf } from '@libraz/coverwise/constraint';
 *
 * // IF os = Windows THEN browser != Safari
 * when('os').eq('Windows').then(when('browser').ne('Safari'))
 *
 * // NOT (os = win AND browser = safari)
 * not(allOf(when('os').eq('win'), when('browser').eq('safari')))
 */

import { CoverwiseError } from './types.js';

// --- Value formatting ---

const KEYWORDS = new Set(['IF', 'THEN', 'ELSE', 'IMPLIES', 'AND', 'OR', 'NOT', 'IN', 'LIKE']);

function isBareIdentifierCharacter(character: string): boolean {
  const codepoint = character.codePointAt(0) ?? 0;
  return (
    (codepoint >= 0x30 && codepoint <= 0x39) ||
    (codepoint >= 0x41 && codepoint <= 0x5a) ||
    (codepoint >= 0x61 && codepoint <= 0x7a) ||
    character === '_' ||
    character === '-' ||
    character === '.' ||
    codepoint >= 0x80
  );
}

function canEmitBare(value: string, allowGlob: boolean): boolean {
  return (
    value.length > 0 &&
    !KEYWORDS.has(value.toUpperCase()) &&
    Array.from(value).every(
      (character) =>
        isBareIdentifierCharacter(character) ||
        (allowGlob && (character === '*' || character === '?')),
    )
  );
}

/**
 * Wrap a value in double quotes, escaping backslashes and embedded double
 * quotes so the emitted string round-trips through the C++ and TS tokenizers,
 * which understand the `\\` and `\"` escapes.
 */
function quote(value: string): string {
  return `"${value.replace(/\\/g, '\\\\').replace(/"/g, '\\"')}"`;
}

function formatNumber(value: number): string {
  if (!Number.isFinite(value)) {
    throw new CoverwiseError('INVALID_INPUT', 'Constraint numbers must be finite');
  }
  return String(value);
}

function formatValue(value: string | number | boolean): string {
  if (typeof value === 'boolean') {
    return String(value);
  }
  if (typeof value === 'number') {
    return formatNumber(value);
  }
  return canEmitBare(value, false) ? value : quote(value);
}

/**
 * Format a relational operand. Numbers are emitted bare; strings are parameter
 * references (the parser resolves a relational RHS string to a parameter name),
 * quoted when they contain whitespace, operators, or other special characters
 * so a name like `end date` round-trips as a single token.
 */
function formatNumericOrParam(value: number | string): string {
  if (typeof value === 'number') {
    return formatNumber(value);
  }
  return canEmitBare(value, false) ? value : quote(value);
}

function formatSetValue(value: string | number | boolean): string {
  if (typeof value === 'boolean') {
    return String(value);
  }
  if (typeof value === 'number') {
    return formatNumber(value);
  }
  return canEmitBare(value, false) ? value : quote(value);
}

/**
 * Format a LIKE pattern. Quoting is applied when the pattern contains
 * whitespace or other special characters; the wildcards `*` and `?` are
 * preserved verbatim inside the quotes because the tokenizer keeps glob chars
 * within a quoted pattern.
 */
function formatPattern(pattern: string): string {
  return canEmitBare(pattern, true) ? pattern : quote(pattern);
}

// --- Interfaces ---

/** A condition expression that can be composed with AND/OR or used in IF...THEN. */
export interface Condition {
  and(other: Condition): Condition;
  or(other: Condition): Condition;
  then(consequence: Condition): Constraint;
  implies(consequence: Condition): Constraint;
  toString(): string;
}

/** A complete constraint (IF...THEN with optional ELSE). */
export interface Constraint {
  else(alternative: Condition): Constraint;
  toString(): string;
}

/** Starting point for building a condition on a parameter. */
export interface ConditionStart {
  eq(value: string | number | boolean): Condition;
  ne(value: string | number | boolean): Condition;
  gt(value: number | string): Condition;
  gte(value: number | string): Condition;
  lt(value: number | string): Condition;
  lte(value: number | string): Condition;
  in(...values: (string | number | boolean)[]): Condition;
  like(pattern: string): Condition;
}

// --- Internal representation ---

type ConditionNode =
  | { kind: 'atom'; expression: string }
  | { kind: 'and'; left: ConditionNode; right: ConditionNode }
  | { kind: 'or'; left: ConditionNode; right: ConditionNode }
  | { kind: 'not'; operand: ConditionNode }
  | { kind: 'opaque'; expression: string };

function nodeFromCondition(condition: Condition): ConditionNode {
  if (condition instanceof ConditionImpl) {
    return condition.node;
  }
  // Condition is a public interface, so callers may provide their own
  // implementation. Keep it usable without attempting to parse its text.
  return { kind: 'opaque', expression: condition.toString() };
}

function renderCondition(node: ConditionNode, parentPrecedence = 0): string {
  let expression: string;
  let precedence: number;

  switch (node.kind) {
    case 'atom':
      expression = node.expression;
      precedence = 4;
      break;
    case 'not':
      expression = `NOT (${renderCondition(node.operand)})`;
      precedence = 3;
      break;
    case 'and':
      expression = `${renderCondition(node.left, 2)} AND ${renderCondition(node.right, 2)}`;
      precedence = 2;
      break;
    case 'or':
      expression = `${renderCondition(node.left, 1)} OR ${renderCondition(node.right, 1)}`;
      precedence = 1;
      break;
    case 'opaque':
      expression = node.expression;
      precedence = 0;
      break;
  }

  return precedence < parentPrecedence ? `(${expression})` : expression;
}

// --- Internal classes ---

class ConditionImpl implements Condition {
  readonly node: ConditionNode;

  constructor(node: ConditionNode) {
    this.node = node;
  }

  and(other: Condition): Condition {
    return new ConditionImpl({ kind: 'and', left: this.node, right: nodeFromCondition(other) });
  }

  or(other: Condition): Condition {
    return new ConditionImpl({ kind: 'or', left: this.node, right: nodeFromCondition(other) });
  }

  // biome-ignore lint/suspicious/noThenProperty: fluent API requires .then() for IF...THEN syntax
  then(consequence: Condition): Constraint {
    return new ConstraintImpl(
      `IF ${renderCondition(this.node)} THEN ${renderCondition(nodeFromCondition(consequence))}`,
    );
  }

  implies(consequence: Condition): Constraint {
    return new ConstraintImpl(
      `${renderCondition(this.node)} IMPLIES ${renderCondition(nodeFromCondition(consequence))}`,
    );
  }

  toString(): string {
    return renderCondition(this.node);
  }
}

class ConstraintImpl implements Constraint {
  private readonly expr: string;

  constructor(expr: string) {
    this.expr = expr;
  }

  else(alternative: Condition): Constraint {
    return new ConstraintImpl(`${this.expr} ELSE ${alternative.toString()}`);
  }

  toString(): string {
    return this.expr;
  }
}

class ConditionStartImpl implements ConditionStart {
  private readonly param: string;

  constructor(param: string) {
    // Quote the parameter name when it contains whitespace, operators, or other
    // special characters so a name like `end date` round-trips as one token.
    this.param = formatNumericOrParam(param);
  }

  eq(value: string | number | boolean): Condition {
    return new ConditionImpl({ kind: 'atom', expression: `${this.param} = ${formatValue(value)}` });
  }

  ne(value: string | number | boolean): Condition {
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} != ${formatValue(value)}`,
    });
  }

  gt(value: number | string): Condition {
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} > ${formatNumericOrParam(value)}`,
    });
  }

  gte(value: number | string): Condition {
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} >= ${formatNumericOrParam(value)}`,
    });
  }

  lt(value: number | string): Condition {
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} < ${formatNumericOrParam(value)}`,
    });
  }

  lte(value: number | string): Condition {
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} <= ${formatNumericOrParam(value)}`,
    });
  }

  in(...values: (string | number | boolean)[]): Condition {
    // Reject an empty set at construction rather than emitting `IN {}`, which
    // would only fail later when the generator parses the constraint.
    if (values.length === 0) {
      throw new CoverwiseError('INVALID_INPUT', 'in() requires at least one value');
    }
    const formatted = values.map(formatSetValue).join(', ');
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} IN {${formatted}}`,
    });
  }

  like(pattern: string): Condition {
    return new ConditionImpl({
      kind: 'atom',
      expression: `${this.param} LIKE ${formatPattern(pattern)}`,
    });
  }
}

// --- Public API ---

/**
 * Start building a condition on a parameter.
 *
 * @param param - The parameter name.
 * @returns A builder for specifying the comparison operator and value.
 *
 * @example
 * when('os').eq('Windows')         // os = Windows
 * when('version').gt(3)            // version > 3
 * when('start_date').lt('end_date') // start_date < end_date
 */
export function when(param: string): ConditionStart {
  return new ConditionStartImpl(param);
}

/**
 * Negate a condition.
 *
 * @example
 * not(when('os').eq('win'))
 * // NOT (os = win)
 *
 * not(allOf(when('os').eq('win'), when('browser').eq('safari')))
 * // NOT (os = win AND browser = safari)
 */
export function not(condition: Condition): Condition {
  return new ConditionImpl({ kind: 'not', operand: nodeFromCondition(condition) });
}

/**
 * Combine conditions with AND.
 *
 * @example
 * allOf(when('os').eq('win'), when('browser').eq('chrome'))
 * // os = win AND browser = chrome
 */
export function allOf(...conditions: Condition[]): Condition {
  if (conditions.length === 0) {
    throw new CoverwiseError('INVALID_INPUT', 'allOf requires at least one condition');
  }
  if (conditions.length === 1) {
    return conditions[0];
  }
  return conditions
    .slice(1)
    .reduce((combined, condition) => combined.and(condition), conditions[0]);
}

/**
 * Combine conditions with OR.
 *
 * @example
 * anyOf(when('os').eq('win'), when('os').eq('linux'))
 * // os = win OR os = linux
 */
export function anyOf(...conditions: Condition[]): Condition {
  if (conditions.length === 0) {
    throw new CoverwiseError('INVALID_INPUT', 'anyOf requires at least one condition');
  }
  if (conditions.length === 1) {
    return conditions[0];
  }
  return conditions.slice(1).reduce((combined, condition) => combined.or(condition), conditions[0]);
}
