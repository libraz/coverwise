/**
 * Fluent constraint builder for coverwise.
 *
 * Generates constraint strings compatible with the C++ constraint parser.
 *
 * @example
 * import { when, not, allOf, anyOf } from '@libraz/coverwise/constraint';
 *
 * // IF os = "Windows" THEN browser != "Safari"
 * when('os').eq('Windows').then(when('browser').ne('Safari'))
 *
 * // NOT (os = "win" AND browser = "safari")
 * not(allOf(when('os').eq('win'), when('browser').eq('safari')))
 */

import { asciiToUpper } from '../src/ts/util/string_util.js';
import { CoverwiseError } from './types.js';
import { entryFrame, readingInput } from './validation.js';

/** Run a builder step inside the package's error frame; nothing but a CoverwiseError escapes. */
function built<T>(step: () => T): T {
  return entryFrame(() => readingInput(step));
}

function invalidOperand(builder: string, expected: string): never {
  throw new CoverwiseError('INVALID_INPUT', `${builder} requires ${expected}`);
}

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

function isAsciiDigit(character: string | undefined): boolean {
  return character !== undefined && character >= '0' && character <= '9';
}

/**
 * Whether the constraint tokenizer reads this text as a number rather than as a
 * name.
 *
 * Mirrors the tokenizer's decimal scan: text the scan consumes end to end
 * becomes a number token, and a number token is a literal — there is no
 * parameter it can refer to. Text where the scan stops with name characters
 * still to come (`3d`, `1.2.3`) is re-read as one identifier and is safe to
 * emit bare. Signs and a leading dot are accepted here even though the
 * tokenizer only accepts them after a comparison operator: quoting a name in a
 * position where it would have been an identifier anyway costs nothing, and
 * emitting one in a position where it would have been a number costs the
 * caller their constraint.
 */
function scansAsNumber(value: string): boolean {
  let i = 0;
  if (value[i] === '+' || value[i] === '-') {
    i += 1;
  }
  let digits = 0;
  while (isAsciiDigit(value[i])) {
    digits += 1;
    i += 1;
  }
  if (value[i] === '.') {
    i += 1;
    while (isAsciiDigit(value[i])) {
      digits += 1;
      i += 1;
    }
  }
  if (digits === 0) {
    return false;
  }
  if (value[i] === 'e' || value[i] === 'E') {
    const exponentStart = i;
    i += 1;
    if (value[i] === '+' || value[i] === '-') {
      i += 1;
    }
    const exponentDigitsStart = i;
    while (isAsciiDigit(value[i])) {
      i += 1;
    }
    if (i === exponentDigitsStart) {
      i = exponentStart;
    }
  }
  return i === value.length;
}

function canEmitBare(value: string, allowGlob: boolean): boolean {
  return (
    value.length > 0 &&
    // The tokenizer classifies keywords by the ASCII fold, so this asks the
    // question with the same fold. A full-Unicode uppercase answers a different
    // one — `ıf` and `ﬁ` uppercase into keywords the tokenizer never sees.
    !KEYWORDS.has(asciiToUpper(value)) &&
    !scansAsNumber(value) &&
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

/** Where a value operand lands, which decides how the parser reads it. */
type ValuePosition = 'comparison' | 'set';

/**
 * Format a value operand for the position it lands in.
 *
 * Every value kind becomes text first and the position alone decides quoting,
 * so the rule cannot hold for one kind and be forgotten for its neighbour, and
 * a kind added later inherits it in both positions.
 *
 * On the right of `=` or `!=` a bare token is resolved as a parameter
 * reference whenever a parameter bears that name — a number token as much as a
 * word — so text is quoted there unconditionally. A quoted operand is still
 * matched against the parameter's values, so quoting costs no legitimate
 * comparison; leaving it bare is what would cost one, turning `p = true` in a
 * model that also declares a parameter named `true` into a
 * parameter-to-parameter comparison instead of reporting an unknown value.
 *
 * Inside an `IN` set a member resolves only against the left parameter's
 * values, with no parameter reference for it to be confused with, so a member
 * keeps its bare spelling whenever it survives as one token.
 */
function formatValue(
  value: string | number | boolean,
  position: ValuePosition,
  builder: string,
): string {
  if (typeof value !== 'string' && typeof value !== 'number' && typeof value !== 'boolean') {
    invalidOperand(builder, 'a string, number, or boolean operand');
  }
  const text = typeof value === 'number' ? formatNumber(value) : String(value);
  return position === 'set' && canEmitBare(text, false) ? text : quote(text);
}

/**
 * Format a parameter name for a position that is read as a name. Quoted when
 * the name contains whitespace, operators, or other characters that would not
 * survive as one bare token, and when it is spelled the way the tokenizer reads
 * a number. A quoted token is still resolved as a name here, which is what
 * makes the quoting safe.
 */
function formatParameterName(name: string): string {
  return canEmitBare(name, false) ? name : quote(name);
}

/**
 * Format a relational operand. A number renders as a literal; a string is a
 * parameter reference.
 *
 * A relational operand is resolved as a name only while it is a bare token — a
 * quoted one is a value, by the same rule `=` and `!=` follow. A name that
 * cannot be written bare therefore has no spelling here at all, and is refused
 * rather than emitted as a comparison the caller did not write: a parameter
 * named `1` would otherwise become the literal comparison `> 1`, which is
 * satisfiable, silent, and covers a different suite.
 */
function formatRelationalOperand(value: number | string, builder: string): string {
  if (typeof value === 'number') {
    return formatNumber(value);
  }
  if (typeof value !== 'string') {
    invalidOperand(builder, 'a number or a parameter name');
  }
  if (!canEmitBare(value, false)) {
    throw new CoverwiseError(
      'INVALID_INPUT',
      `Parameter name '${value}' cannot be a relational operand: only a name written as one bare token is read as a parameter there. Pass a number to compare against a value.`,
    );
  }
  return value;
}

/**
 * Format a LIKE pattern. Quoting is applied when the pattern contains
 * whitespace or other special characters; the wildcards `*` and `?` are
 * preserved verbatim inside the quotes because the tokenizer keeps glob chars
 * within a quoted pattern.
 */
function formatPattern(pattern: string): string {
  if (typeof pattern !== 'string') {
    invalidOperand('like()', 'a string pattern');
  }
  return canEmitBare(pattern, true) ? pattern : quote(pattern);
}

// --- Interfaces ---

/** A condition expression that can be composed with AND/OR or used in IF...THEN. */
export interface Condition {
  and(other: Condition): Condition;
  or(other: Condition): Condition;
  then(consequence: Condition): IfConstraint;
  implies(consequence: Condition): Constraint;
  toString(): string;
}

/** A complete constraint expression, ready to be passed as a constraint string. */
export interface Constraint {
  toString(): string;
}

/**
 * An `IF ... THEN` constraint, the only form the grammar lets an `ELSE` branch
 * follow. `else()` yields a plain {@link Constraint}, because a second `ELSE`
 * has no reading in the grammar and so must not be constructible.
 */
export interface IfConstraint extends Constraint {
  else(alternative: Condition): Constraint;
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

function nodeFromCondition(condition: Condition, builder: string): ConditionNode {
  if (condition instanceof ConditionImpl) {
    return condition.node;
  }
  // Condition is a public interface, so callers may provide their own
  // implementation. Keep it usable without attempting to parse its text; a
  // value with nothing of its own to say is not one.
  if (
    typeof condition !== 'object' ||
    condition === null ||
    typeof condition.toString !== 'function' ||
    condition.toString === Object.prototype.toString
  ) {
    invalidOperand(builder, 'a Condition');
  }
  const expression: unknown = condition.toString();
  if (typeof expression !== 'string') {
    invalidOperand(builder, 'a Condition');
  }
  return { kind: 'opaque', expression };
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
    return built(
      () =>
        new ConditionImpl({
          kind: 'and',
          left: this.node,
          right: nodeFromCondition(other, 'and()'),
        }),
    );
  }

  or(other: Condition): Condition {
    return built(
      () =>
        new ConditionImpl({
          kind: 'or',
          left: this.node,
          right: nodeFromCondition(other, 'or()'),
        }),
    );
  }

  // biome-ignore lint/suspicious/noThenProperty: fluent API requires .then() for IF...THEN syntax
  then(consequence: Condition): IfConstraint {
    return built(
      () =>
        new ConstraintImpl(
          `IF ${renderCondition(this.node)} THEN ${renderCondition(nodeFromCondition(consequence, 'then()'))}`,
        ),
    );
  }

  implies(consequence: Condition): Constraint {
    return built(
      () =>
        new ConstraintImpl(
          `${renderCondition(this.node)} IMPLIES ${renderCondition(nodeFromCondition(consequence, 'implies()'))}`,
        ),
    );
  }

  toString(): string {
    return built(() => renderCondition(this.node));
  }
}

class ConstraintImpl implements IfConstraint {
  private readonly expr: string;

  constructor(expr: string) {
    this.expr = expr;
  }

  else(alternative: Condition): Constraint {
    return built(
      () =>
        new ConstraintImpl(
          `${this.expr} ELSE ${renderCondition(nodeFromCondition(alternative, 'else()'))}`,
        ),
    );
  }

  toString(): string {
    return this.expr;
  }
}

class ConditionStartImpl implements ConditionStart {
  private readonly param: string;

  constructor(param: string) {
    if (typeof param !== 'string') {
      invalidOperand('when()', 'a parameter name string');
    }
    // Quote the parameter name when it contains whitespace, operators, or other
    // special characters so a name like `end date` round-trips as one token.
    this.param = formatParameterName(param);
  }

  eq(value: string | number | boolean): Condition {
    return this.compare('=', value, 'eq()');
  }

  ne(value: string | number | boolean): Condition {
    return this.compare('!=', value, 'ne()');
  }

  gt(value: number | string): Condition {
    return this.relate('>', value, 'gt()');
  }

  gte(value: number | string): Condition {
    return this.relate('>=', value, 'gte()');
  }

  lt(value: number | string): Condition {
    return this.relate('<', value, 'lt()');
  }

  lte(value: number | string): Condition {
    return this.relate('<=', value, 'lte()');
  }

  in(...values: (string | number | boolean)[]): Condition {
    return built(() => {
      // Reject an empty set at construction rather than emitting `IN {}`, which
      // would only fail later when the generator parses the constraint.
      if (values.length === 0) {
        throw new CoverwiseError('INVALID_INPUT', 'in() requires at least one value');
      }
      const formatted = values.map((value) => formatValue(value, 'set', 'in()')).join(', ');
      return new ConditionImpl({
        kind: 'atom',
        expression: `${this.param} IN {${formatted}}`,
      });
    });
  }

  like(pattern: string): Condition {
    return built(
      () =>
        new ConditionImpl({
          kind: 'atom',
          expression: `${this.param} LIKE ${formatPattern(pattern)}`,
        }),
    );
  }

  private compare(
    operator: '=' | '!=',
    value: string | number | boolean,
    builder: string,
  ): Condition {
    return built(
      () =>
        new ConditionImpl({
          kind: 'atom',
          expression: `${this.param} ${operator} ${formatValue(value, 'comparison', builder)}`,
        }),
    );
  }

  private relate(
    operator: '>' | '>=' | '<' | '<=',
    value: number | string,
    builder: string,
  ): Condition {
    return built(
      () =>
        new ConditionImpl({
          kind: 'atom',
          expression: `${this.param} ${operator} ${formatRelationalOperand(value, builder)}`,
        }),
    );
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
 * when('os').eq('Windows')         // os = "Windows"
 * when('version').gt(3)            // version > 3
 * when('start_date').lt('end_date') // start_date < end_date
 */
export function when(param: string): ConditionStart {
  return built(() => new ConditionStartImpl(param));
}

/**
 * Negate a condition.
 *
 * @example
 * not(when('os').eq('win'))
 * // NOT (os = "win")
 *
 * not(allOf(when('os').eq('win'), when('browser').eq('safari')))
 * // NOT (os = "win" AND browser = "safari")
 */
export function not(condition: Condition): Condition {
  return built(
    () => new ConditionImpl({ kind: 'not', operand: nodeFromCondition(condition, 'not()') }),
  );
}

/**
 * Combine conditions with AND.
 *
 * @example
 * allOf(when('os').eq('win'), when('browser').eq('chrome'))
 * // os = "win" AND browser = "chrome"
 */
export function allOf(...conditions: Condition[]): Condition {
  return built(() => {
    if (conditions.length === 0) {
      throw new CoverwiseError('INVALID_INPUT', 'allOf requires at least one condition');
    }
    for (const condition of conditions) {
      nodeFromCondition(condition, 'allOf()');
    }
    if (conditions.length === 1) {
      return conditions[0];
    }
    return conditions
      .slice(1)
      .reduce((combined, condition) => combined.and(condition), conditions[0]);
  });
}

/**
 * Combine conditions with OR.
 *
 * @example
 * anyOf(when('os').eq('win'), when('os').eq('linux'))
 * // os = "win" OR os = "linux"
 */
export function anyOf(...conditions: Condition[]): Condition {
  return built(() => {
    if (conditions.length === 0) {
      throw new CoverwiseError('INVALID_INPUT', 'anyOf requires at least one condition');
    }
    for (const condition of conditions) {
      nodeFromCondition(condition, 'anyOf()');
    }
    if (conditions.length === 1) {
      return conditions[0];
    }
    return conditions
      .slice(1)
      .reduce((combined, condition) => combined.or(condition), conditions[0]);
  });
}
