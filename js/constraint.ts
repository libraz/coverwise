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

// --- Value formatting ---

const NEEDS_QUOTE_RE = /[\s=!<>(),{}]/;
const KEYWORDS = new Set(['IF', 'THEN', 'ELSE', 'IMPLIES', 'AND', 'OR', 'NOT', 'IN', 'LIKE']);

function formatValue(value: string | number | boolean): string {
  if (typeof value === 'boolean') {
    return String(value);
  }
  if (typeof value === 'number') {
    return String(value);
  }
  if (NEEDS_QUOTE_RE.test(value) || KEYWORDS.has(value.toUpperCase())) {
    return `"${value.replace(/"/g, '\\"')}"`;
  }
  return value;
}

function formatNumericOrParam(value: number | string): string {
  if (typeof value === 'number') {
    return String(value);
  }
  // String = param name, output bare
  return value;
}

function formatSetValue(value: string | number | boolean): string {
  if (typeof value === 'boolean') {
    return String(value);
  }
  if (typeof value === 'number') {
    return String(value);
  }
  if (NEEDS_QUOTE_RE.test(value) || KEYWORDS.has(value.toUpperCase())) {
    return `"${value.replace(/"/g, '\\"')}"`;
  }
  return value;
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

// --- Internal classes ---

class ConditionImpl implements Condition {
  private readonly expr: string;

  constructor(expr: string) {
    this.expr = expr;
  }

  and(other: Condition): Condition {
    return new ConditionImpl(`${this.wrap()} AND ${wrapCondition(other)}`);
  }

  or(other: Condition): Condition {
    return new ConditionImpl(`${this.expr} OR ${wrapCondition(other)}`);
  }

  // biome-ignore lint/suspicious/noThenProperty: fluent API requires .then() for IF...THEN syntax
  then(consequence: Condition): Constraint {
    return new ConstraintImpl(`IF ${this.expr} THEN ${consequence.toString()}`);
  }

  implies(consequence: Condition): Constraint {
    return new ConstraintImpl(`${this.expr} IMPLIES ${consequence.toString()}`);
  }

  toString(): string {
    return this.expr;
  }

  private wrap(): string {
    // Wrap in parens if the expression contains OR (for correct AND precedence)
    if (needsParensForAnd(this.expr)) {
      return `(${this.expr})`;
    }
    return this.expr;
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
    this.param = param;
  }

  eq(value: string | number | boolean): Condition {
    return new ConditionImpl(`${this.param} = ${formatValue(value)}`);
  }

  ne(value: string | number | boolean): Condition {
    return new ConditionImpl(`${this.param} != ${formatValue(value)}`);
  }

  gt(value: number | string): Condition {
    return new ConditionImpl(`${this.param} > ${formatNumericOrParam(value)}`);
  }

  gte(value: number | string): Condition {
    return new ConditionImpl(`${this.param} >= ${formatNumericOrParam(value)}`);
  }

  lt(value: number | string): Condition {
    return new ConditionImpl(`${this.param} < ${formatNumericOrParam(value)}`);
  }

  lte(value: number | string): Condition {
    return new ConditionImpl(`${this.param} <= ${formatNumericOrParam(value)}`);
  }

  in(...values: (string | number | boolean)[]): Condition {
    const formatted = values.map(formatSetValue).join(', ');
    return new ConditionImpl(`${this.param} IN {${formatted}}`);
  }

  like(pattern: string): Condition {
    return new ConditionImpl(`${this.param} LIKE ${pattern}`);
  }
}

// --- Helpers ---

/** Check if an expression string contains a bare OR (not inside parens). */
function needsParensForAnd(expr: string): boolean {
  let depth = 0;
  const upper = expr.toUpperCase();
  for (let i = 0; i < upper.length; i++) {
    if (upper[i] === '(') {
      depth++;
    } else if (upper[i] === ')') {
      depth--;
    } else if (depth === 0 && upper[i] === ' ' && upper.substring(i, i + 4) === ' OR ') {
      return true;
    }
  }
  return false;
}

/** Wrap a condition for use as an operand, adding parens if it contains AND or OR at top level. */
function wrapCondition(cond: Condition): string {
  const s = cond.toString();
  if (needsParensForAnd(s)) {
    return `(${s})`;
  }
  return s;
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
  return new ConditionImpl(`NOT (${condition.toString()})`);
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
    throw new Error('allOf requires at least one condition');
  }
  if (conditions.length === 1) {
    return conditions[0];
  }
  const parts = conditions.map((c) => {
    const s = c.toString();
    if (needsParensForAnd(s)) {
      return `(${s})`;
    }
    return s;
  });
  return new ConditionImpl(parts.join(' AND '));
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
    throw new Error('anyOf requires at least one condition');
  }
  if (conditions.length === 1) {
    return conditions[0];
  }
  const parts = conditions.map((c) => c.toString());
  return new ConditionImpl(parts.join(' OR '));
}
