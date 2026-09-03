/// AST-based constraint representation for combinatorial test generation.

import { asciiToUpper, isNumeric, toDouble } from '../util/string_util.js';
import { MAX_VALUES_PER_PARAMETER } from './limits.js';
import { UNASSIGNED } from './test-case.js';

export { UNASSIGNED };

/** Result of evaluating a constraint against a partial assignment. */
export enum ConstraintResult {
  True = 'true',
  False = 'false',
  Unknown = 'unknown',
}

/** Base interface for constraint AST nodes. */
export interface ConstraintNode {
  /** Evaluate this constraint against a (possibly partial) assignment. */
  evaluate(assignment: number[]): ConstraintResult;
  /**
   * Render this constraint back to a human-readable string for diagnostics
   * (e.g. violation descriptions). When a node was built with parameter/value
   * names they are used; otherwise an index-based form (`p0 = v1`) is produced.
   */
  toString(): string;
}

/** Render a parameter reference, preferring its name when available. */
function paramLabel(index: number, name?: string): string {
  return name ?? `p${index}`;
}

/** Render a value reference, preferring its name when available. */
function valueLabel(index: number, name?: string): string {
  return name ?? `v${index}`;
}

/** Equality comparison: param_index == value_index. */
export class EqualsNode implements ConstraintNode {
  constructor(
    readonly paramIndex: number,
    readonly valueIndex: number,
    private readonly paramName?: string,
    private readonly valueName?: string,
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    if (this.paramIndex >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const val = assignment[this.paramIndex];
    if (val === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    return val === this.valueIndex ? ConstraintResult.True : ConstraintResult.False;
  }

  toString(): string {
    return `${paramLabel(this.paramIndex, this.paramName)} = ${valueLabel(this.valueIndex, this.valueName)}`;
  }
}

/** Inequality comparison: param_index != value_index. */
export class NotEqualsNode implements ConstraintNode {
  constructor(
    readonly paramIndex: number,
    readonly valueIndex: number,
    private readonly paramName?: string,
    private readonly valueName?: string,
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    if (this.paramIndex >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const val = assignment[this.paramIndex];
    if (val === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    return val !== this.valueIndex ? ConstraintResult.True : ConstraintResult.False;
  }

  toString(): string {
    return `${paramLabel(this.paramIndex, this.paramName)} != ${valueLabel(this.valueIndex, this.valueName)}`;
  }
}

/** Logical AND of two sub-expressions. */
export class AndNode implements ConstraintNode {
  constructor(
    private readonly left: ConstraintNode,
    private readonly right: ConstraintNode,
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    const l = this.left.evaluate(assignment);
    if (l === ConstraintResult.False) {
      return ConstraintResult.False;
    }
    const r = this.right.evaluate(assignment);
    if (r === ConstraintResult.False) {
      return ConstraintResult.False;
    }
    if (l === ConstraintResult.True && r === ConstraintResult.True) {
      return ConstraintResult.True;
    }
    return ConstraintResult.Unknown;
  }

  toString(): string {
    return `(${this.left.toString()} AND ${this.right.toString()})`;
  }
}

/** Logical OR of two sub-expressions. */
export class OrNode implements ConstraintNode {
  constructor(
    private readonly left: ConstraintNode,
    private readonly right: ConstraintNode,
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    const l = this.left.evaluate(assignment);
    if (l === ConstraintResult.True) {
      return ConstraintResult.True;
    }
    const r = this.right.evaluate(assignment);
    if (r === ConstraintResult.True) {
      return ConstraintResult.True;
    }
    if (l === ConstraintResult.False && r === ConstraintResult.False) {
      return ConstraintResult.False;
    }
    return ConstraintResult.Unknown;
  }

  toString(): string {
    return `(${this.left.toString()} OR ${this.right.toString()})`;
  }
}

/** Logical NOT of a sub-expression. */
export class NotNode implements ConstraintNode {
  constructor(private readonly child: ConstraintNode) {}

  evaluate(assignment: number[]): ConstraintResult {
    const c = this.child.evaluate(assignment);
    if (c === ConstraintResult.True) {
      return ConstraintResult.False;
    }
    if (c === ConstraintResult.False) {
      return ConstraintResult.True;
    }
    return ConstraintResult.Unknown;
  }

  toString(): string {
    return `NOT (${this.child.toString()})`;
  }
}

/** Logical implication: antecedent IMPLIES consequent (= NOT antecedent OR consequent). */
export class ImpliesNode implements ConstraintNode {
  constructor(
    private readonly antecedent: ConstraintNode,
    private readonly consequent: ConstraintNode,
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    const ante = this.antecedent.evaluate(assignment);
    if (ante === ConstraintResult.False) {
      return ConstraintResult.True;
    }
    const cons = this.consequent.evaluate(assignment);
    if (ante === ConstraintResult.True) {
      return cons;
    }
    // ante is Unknown
    if (cons === ConstraintResult.True) {
      return ConstraintResult.True;
    }
    return ConstraintResult.Unknown;
  }

  toString(): string {
    return `(${this.antecedent.toString()} IMPLIES ${this.consequent.toString()})`;
  }
}

/**
 * IF/THEN/ELSE ternary constraint.
 *
 * Three-valued evaluation:
 * - condition=True  -> evaluate then_branch
 * - condition=False -> evaluate else_branch
 * - condition=Unknown -> if both branches agree, use that; else Unknown
 */
export class IfThenElseNode implements ConstraintNode {
  constructor(
    private readonly condition: ConstraintNode,
    private readonly thenBranch: ConstraintNode,
    private readonly elseBranch: ConstraintNode,
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    const cond = this.condition.evaluate(assignment);
    if (cond === ConstraintResult.True) {
      return this.thenBranch.evaluate(assignment);
    }
    if (cond === ConstraintResult.False) {
      return this.elseBranch.evaluate(assignment);
    }
    // condition is Unknown: evaluate both branches
    const thenResult = this.thenBranch.evaluate(assignment);
    const elseResult = this.elseBranch.evaluate(assignment);
    if (thenResult === elseResult) {
      return thenResult;
    }
    return ConstraintResult.Unknown;
  }

  toString(): string {
    return `IF ${this.condition.toString()} THEN ${this.thenBranch.toString()} ELSE ${this.elseBranch.toString()}`;
  }
}

/** Relational comparison operators. */
export enum RelOp {
  Less = '<',
  LessEqual = '<=',
  Greater = '>',
  GreaterEqual = '>=',
}

/** Immutable numeric parsing cache shared by relational atoms. */
export interface NumericValueCache {
  numeric: Float64Array;
  valid: Uint8Array;
}

/**
 * Relational comparison of a parameter's numeric value against a literal or another parameter.
 *
 * Compares parameter values as numbers. If a value cannot be parsed as numeric,
 * the result is False.
 */
export class RelationalNode implements ConstraintNode {
  private readonly leftParam: number;
  private readonly op: RelOp;
  private readonly isParamComparison: boolean;
  private readonly literal: number;
  private readonly rightParam: number;
  // Numeric conversions precomputed at construction so evaluate() never
  // reparses on the hot path (mirrors LikeNode's precomputed matching). Parallel
  // to the value index: `*Valid` marks numeric strings, `*Numeric` holds the
  // parsed value (0 for non-numeric, which evaluate() never reads).
  private readonly leftNumeric: Float64Array;
  private readonly leftValid: Uint8Array;
  private readonly rightNumeric: Float64Array;
  private readonly rightValid: Uint8Array;

  /** Compare a parameter value against a literal numeric value. */
  static fromLiteral(
    paramIndex: number,
    op: RelOp,
    literal: number,
    paramValues: string[],
    paramCache?: NumericValueCache,
  ): RelationalNode {
    return new RelationalNode(paramIndex, op, false, literal, 0, paramValues, [], paramCache);
  }

  /** Compare two parameter values against each other. */
  static fromParams(
    leftParam: number,
    op: RelOp,
    rightParam: number,
    leftValues: string[],
    rightValues: string[],
    leftCache?: NumericValueCache,
    rightCache?: NumericValueCache,
  ): RelationalNode {
    return new RelationalNode(
      leftParam,
      op,
      true,
      0,
      rightParam,
      leftValues,
      rightValues,
      leftCache,
      rightCache,
    );
  }

  private constructor(
    leftParam: number,
    op: RelOp,
    isParamComparison: boolean,
    literal: number,
    rightParam: number,
    leftValues: string[],
    rightValues: string[],
    leftCache?: NumericValueCache,
    rightCache?: NumericValueCache,
  ) {
    this.leftParam = leftParam;
    this.op = op;
    this.isParamComparison = isParamComparison;
    this.literal = literal;
    this.rightParam = rightParam;
    const left = leftCache ?? RelationalNode.createNumericCache(leftValues);
    const right = rightCache ?? RelationalNode.createNumericCache(rightValues);
    this.leftNumeric = left.numeric;
    this.leftValid = left.valid;
    this.rightNumeric = right.numeric;
    this.rightValid = right.valid;
  }

  /** Precompute numeric conversions for a parameter's value strings. */
  static createNumericCache(values: string[]): NumericValueCache {
    const numeric = new Float64Array(values.length);
    const valid = new Uint8Array(values.length);
    for (let i = 0; i < values.length; i++) {
      if (isNumeric(values[i])) {
        valid[i] = 1;
        numeric[i] = toDouble(values[i]);
      }
    }
    return { numeric, valid };
  }

  evaluate(assignment: number[]): ConstraintResult {
    if (this.leftParam >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const leftVal = assignment[this.leftParam];
    if (leftVal === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }

    if (leftVal >= this.leftValid.length || this.leftValid[leftVal] === 0) {
      return ConstraintResult.False;
    }
    const leftNum = this.leftNumeric[leftVal];

    if (this.isParamComparison) {
      if (this.rightParam >= assignment.length) {
        return ConstraintResult.Unknown;
      }
      const rightVal = assignment[this.rightParam];
      if (rightVal === UNASSIGNED) {
        return ConstraintResult.Unknown;
      }
      if (rightVal >= this.rightValid.length || this.rightValid[rightVal] === 0) {
        return ConstraintResult.False;
      }
      const rightNum = this.rightNumeric[rightVal];
      return this.compareValues(leftNum, rightNum) ? ConstraintResult.True : ConstraintResult.False;
    }

    return this.compareValues(leftNum, this.literal)
      ? ConstraintResult.True
      : ConstraintResult.False;
  }

  private compareValues(left: number, right: number): boolean {
    switch (this.op) {
      case RelOp.Less:
        return left < right;
      case RelOp.LessEqual:
        return left <= right;
      case RelOp.Greater:
        return left > right;
      case RelOp.GreaterEqual:
        return left >= right;
    }
  }

  toString(): string {
    const left = paramLabel(this.leftParam);
    const right = this.isParamComparison ? paramLabel(this.rightParam) : String(this.literal);
    return `${left} ${this.op} ${right}`;
  }
}

/**
 * IN-set membership test: param IN {val1, val2, ...}.
 *
 * Three-valued: unassigned -> Unknown, value in set -> True, else False.
 * Membership is precomputed at construction time, so evaluation costs the same
 * whatever the size of the set.
 */
export class InNode implements ConstraintNode {
  /**
   * Membership by value index, the same way LikeNode precomputes its matches.
   * Sized to the largest index the set actually holds, and never past the
   * largest a parameter may have. A value index past the end is not in the set,
   * which is the answer an entry would have carried anyway.
   */
  private readonly members: boolean[];

  constructor(
    private readonly paramIndex: number,
    private readonly valueIndices: number[],
  ) {
    // An index no parameter can hold is not a member of anything, so it is left
    // out rather than allowed to size the table. That covers UNASSIGNED, which
    // evaluate answers Unknown for before it ever reaches the table, and every
    // index at or past MAX_VALUES_PER_PARAMETER, which no value of a
    // well-formed model occupies.
    const inDomain = (vi: number): boolean => vi >= 0 && vi < MAX_VALUES_PER_PARAMETER;
    let highest = 0;
    for (const vi of valueIndices) {
      if (inDomain(vi) && vi > highest) {
        highest = vi;
      }
    }
    this.members = new Array<boolean>(highest + 1).fill(false);
    for (const vi of valueIndices) {
      if (inDomain(vi)) {
        this.members[vi] = true;
      }
    }
  }

  evaluate(assignment: number[]): ConstraintResult {
    if (this.paramIndex >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const val = assignment[this.paramIndex];
    if (val === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (val >= this.members.length) {
      return ConstraintResult.False;
    }
    return this.members[val] ? ConstraintResult.True : ConstraintResult.False;
  }

  toString(): string {
    const values = this.valueIndices.map((vi) => valueLabel(vi)).join(', ');
    return `${paramLabel(this.paramIndex)} IN {${values}}`;
  }
}

/**
 * LIKE pattern matching: param LIKE pattern.
 *
 * Supports `*` (any string) and `?` (single character) wildcards.
 * Matching honors caseSensitive so it is consistent with the other
 * value-matching operators (case-insensitive by default).
 * Matching results are precomputed at construction time for efficiency.
 */
export class LikeNode implements ConstraintNode {
  private readonly paramIndex: number;
  private readonly pattern: string;
  private readonly matches: boolean[];

  constructor(paramIndex: number, pattern: string, paramValues: string[], caseSensitive = false) {
    this.paramIndex = paramIndex;
    this.pattern = pattern;
    // Case-insensitive matching folds the pattern and every value once here, so
    // evaluate stays a precomputed lookup. Folding is ASCII-only, the same
    // policy as asciiCaseInsensitiveEqual.
    //
    // The pattern is decomposed into codepoints once, so construction costs
    // (sum of value lengths + pattern length) rather than
    // (value count x pattern length).
    const patternCodepoints = Array.from(caseSensitive ? pattern : asciiToUpper(pattern));
    this.matches = paramValues.map((v) =>
      globMatchCodepoints(patternCodepoints, Array.from(caseSensitive ? v : asciiToUpper(v))),
    );
  }

  evaluate(assignment: number[]): ConstraintResult {
    if (this.paramIndex >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const val = assignment[this.paramIndex];
    if (val === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (val >= this.matches.length) {
      return ConstraintResult.False;
    }
    return this.matches[val] ? ConstraintResult.True : ConstraintResult.False;
  }

  toString(): string {
    return `${paramLabel(this.paramIndex)} LIKE ${this.pattern}`;
  }
}

/** Test whether a string matches a glob pattern (* and ?). */
export function globMatch(pattern: string, text: string): boolean {
  return globMatchCodepoints(Array.from(pattern), Array.from(text));
}

/**
 * Test whether pre-decomposed text matches a pre-decomposed pattern.
 *
 * Callers that match one pattern against many values decompose the pattern once
 * and reuse it.
 */
function globMatchCodepoints(patternCodepoints: string[], textCodepoints: string[]): boolean {
  let pi = 0;
  let ti = 0;
  let starPi = -1;
  let starTi = 0;

  while (ti < textCodepoints.length) {
    if (
      pi < patternCodepoints.length &&
      (patternCodepoints[pi] === '?' || patternCodepoints[pi] === textCodepoints[ti])
    ) {
      pi++;
      ti++;
    } else if (pi < patternCodepoints.length && patternCodepoints[pi] === '*') {
      starPi = pi;
      starTi = ti;
      pi++;
    } else if (starPi !== -1) {
      pi = starPi + 1;
      starTi++;
      ti = starTi;
    } else {
      return false;
    }
  }

  while (pi < patternCodepoints.length && patternCodepoints[pi] === '*') {
    pi++;
  }
  return pi === patternCodepoints.length;
}

/**
 * Comparison keys for one parameter's value strings.
 *
 * Entry `i` is the key of value index `i`. Two values compare equal exactly when
 * their keys are equal, so a key pair produced by a single internValuePair()
 * call may only be compared against its partner.
 */
type ValueKeys = Uint32Array;

/**
 * Intern two parameters' value strings into a comparable key pair.
 *
 * Folding follows caseSensitive, so the policy is baked into the keys and
 * comparisons never look at the strings again.
 */
function internValuePair(
  leftValues: string[],
  rightValues: string[],
  caseSensitive: boolean,
): [ValueKeys, ValueKeys] {
  const table = new Map<string, number>();
  const intern = (values: string[]): ValueKeys => {
    const keys = new Uint32Array(values.length);
    for (let i = 0; i < values.length; i++) {
      const folded = caseSensitive ? values[i] : asciiToUpper(values[i]);
      let key = table.get(folded);
      if (key === undefined) {
        key = table.size;
        table.set(folded, key);
      }
      keys[i] = key;
    }
    return keys;
  };
  return [intern(leftValues), intern(rightValues)];
}

/**
 * Parameter-to-parameter equality comparison.
 *
 * Compares the string values of two parameters. Equal if the string
 * representations match. Matching honors caseSensitive so it is consistent with
 * value-to-literal comparisons (case-insensitive by default).
 */
export class ParamEqualsNode implements ConstraintNode {
  // Value strings are interned at construction, the same way RelationalNode
  // precomputes its numeric conversions, so evaluate compares two integers and
  // never touches a value string.
  private readonly leftKeys: ValueKeys;
  private readonly rightKeys: ValueKeys;

  constructor(
    private readonly leftParam: number,
    private readonly rightParam: number,
    leftValues: string[],
    rightValues: string[],
    caseSensitive = false,
  ) {
    [this.leftKeys, this.rightKeys] = internValuePair(leftValues, rightValues, caseSensitive);
  }

  evaluate(assignment: number[]): ConstraintResult {
    if (this.leftParam >= assignment.length || this.rightParam >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const lv = assignment[this.leftParam];
    const rv = assignment[this.rightParam];
    if (lv === UNASSIGNED || rv === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (lv >= this.leftKeys.length || rv >= this.rightKeys.length) {
      return ConstraintResult.False;
    }
    return this.leftKeys[lv] === this.rightKeys[rv]
      ? ConstraintResult.True
      : ConstraintResult.False;
  }

  toString(): string {
    return `${paramLabel(this.leftParam)} = ${paramLabel(this.rightParam)}`;
  }
}

/**
 * Parameter-to-parameter inequality comparison.
 *
 * Compares the string values of two parameters. Not equal if the string
 * representations differ. Matching honors caseSensitive so it is consistent with
 * value-to-literal comparisons (case-insensitive by default).
 */
export class ParamNotEqualsNode implements ConstraintNode {
  private readonly leftKeys: ValueKeys;
  private readonly rightKeys: ValueKeys;

  constructor(
    private readonly leftParam: number,
    private readonly rightParam: number,
    leftValues: string[],
    rightValues: string[],
    caseSensitive = false,
  ) {
    [this.leftKeys, this.rightKeys] = internValuePair(leftValues, rightValues, caseSensitive);
  }

  evaluate(assignment: number[]): ConstraintResult {
    if (this.leftParam >= assignment.length || this.rightParam >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const lv = assignment[this.leftParam];
    const rv = assignment[this.rightParam];
    if (lv === UNASSIGNED || rv === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (lv >= this.leftKeys.length || rv >= this.rightKeys.length) {
      return ConstraintResult.False;
    }
    return this.leftKeys[lv] === this.rightKeys[rv]
      ? ConstraintResult.False
      : ConstraintResult.True;
  }

  toString(): string {
    return `${paramLabel(this.leftParam)} != ${paramLabel(this.rightParam)}`;
  }
}
