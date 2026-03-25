/// AST-based constraint representation for combinatorial test generation.

import { isNumeric, toDouble } from '../util/string_util.js';
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
}

/** Equality comparison: param_index == value_index. */
export class EqualsNode implements ConstraintNode {
  constructor(
    readonly paramIndex: number,
    readonly valueIndex: number,
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
}

/** Inequality comparison: param_index != value_index. */
export class NotEqualsNode implements ConstraintNode {
  constructor(
    readonly paramIndex: number,
    readonly valueIndex: number,
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
}

/** Relational comparison operators. */
export enum RelOp {
  Less = '<',
  LessEqual = '<=',
  Greater = '>',
  GreaterEqual = '>=',
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
  private readonly leftValues: string[];
  private readonly rightValues: string[];

  /** Compare a parameter value against a literal numeric value. */
  static fromLiteral(
    paramIndex: number,
    op: RelOp,
    literal: number,
    paramValues: string[],
  ): RelationalNode {
    return new RelationalNode(paramIndex, op, false, literal, 0, paramValues, []);
  }

  /** Compare two parameter values against each other. */
  static fromParams(
    leftParam: number,
    op: RelOp,
    rightParam: number,
    leftValues: string[],
    rightValues: string[],
  ): RelationalNode {
    return new RelationalNode(leftParam, op, true, 0, rightParam, leftValues, rightValues);
  }

  private constructor(
    leftParam: number,
    op: RelOp,
    isParamComparison: boolean,
    literal: number,
    rightParam: number,
    leftValues: string[],
    rightValues: string[],
  ) {
    this.leftParam = leftParam;
    this.op = op;
    this.isParamComparison = isParamComparison;
    this.literal = literal;
    this.rightParam = rightParam;
    this.leftValues = leftValues;
    this.rightValues = rightValues;
  }

  evaluate(assignment: number[]): ConstraintResult {
    if (this.leftParam >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const leftVal = assignment[this.leftParam];
    if (leftVal === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }

    if (leftVal >= this.leftValues.length || !isNumeric(this.leftValues[leftVal])) {
      return ConstraintResult.False;
    }
    const leftNum = toDouble(this.leftValues[leftVal]);

    if (this.isParamComparison) {
      if (this.rightParam >= assignment.length) {
        return ConstraintResult.Unknown;
      }
      const rightVal = assignment[this.rightParam];
      if (rightVal === UNASSIGNED) {
        return ConstraintResult.Unknown;
      }
      if (rightVal >= this.rightValues.length || !isNumeric(this.rightValues[rightVal])) {
        return ConstraintResult.False;
      }
      const rightNum = toDouble(this.rightValues[rightVal]);
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
}

/**
 * IN-set membership test: param IN {val1, val2, ...}.
 *
 * Three-valued: unassigned -> Unknown, value in set -> True, else False.
 */
export class InNode implements ConstraintNode {
  constructor(
    private readonly paramIndex: number,
    private readonly valueIndices: number[],
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    if (this.paramIndex >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const val = assignment[this.paramIndex];
    if (val === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    for (const vi of this.valueIndices) {
      if (val === vi) {
        return ConstraintResult.True;
      }
    }
    return ConstraintResult.False;
  }
}

/**
 * LIKE pattern matching: param LIKE pattern.
 *
 * Supports `*` (any string) and `?` (single character) wildcards.
 * Matching results are precomputed at construction time for efficiency.
 */
export class LikeNode implements ConstraintNode {
  private readonly paramIndex: number;
  private readonly matches: boolean[];

  constructor(paramIndex: number, pattern: string, paramValues: string[]) {
    this.paramIndex = paramIndex;
    this.matches = paramValues.map((v) => globMatch(pattern, v));
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
}

/** Test whether a string matches a glob pattern (* and ?). */
export function globMatch(pattern: string, text: string): boolean {
  let pi = 0;
  let ti = 0;
  let starPi = -1;
  let starTi = 0;

  while (ti < text.length) {
    if (pi < pattern.length && (pattern[pi] === '?' || pattern[pi] === text[ti])) {
      pi++;
      ti++;
    } else if (pi < pattern.length && pattern[pi] === '*') {
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

  while (pi < pattern.length && pattern[pi] === '*') {
    pi++;
  }
  return pi === pattern.length;
}

/**
 * Parameter-to-parameter equality comparison.
 *
 * Compares the string values of two parameters. Equal if the string
 * representations are identical.
 */
export class ParamEqualsNode implements ConstraintNode {
  constructor(
    private readonly leftParam: number,
    private readonly rightParam: number,
    private readonly leftValues: string[],
    private readonly rightValues: string[],
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    if (this.leftParam >= assignment.length || this.rightParam >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const lv = assignment[this.leftParam];
    const rv = assignment[this.rightParam];
    if (lv === UNASSIGNED || rv === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (lv >= this.leftValues.length || rv >= this.rightValues.length) {
      return ConstraintResult.False;
    }
    return this.leftValues[lv] === this.rightValues[rv]
      ? ConstraintResult.True
      : ConstraintResult.False;
  }
}

/**
 * Parameter-to-parameter inequality comparison.
 *
 * Compares the string values of two parameters. Not equal if the string
 * representations differ.
 */
export class ParamNotEqualsNode implements ConstraintNode {
  constructor(
    private readonly leftParam: number,
    private readonly rightParam: number,
    private readonly leftValues: string[],
    private readonly rightValues: string[],
  ) {}

  evaluate(assignment: number[]): ConstraintResult {
    if (this.leftParam >= assignment.length || this.rightParam >= assignment.length) {
      return ConstraintResult.Unknown;
    }
    const lv = assignment[this.leftParam];
    const rv = assignment[this.rightParam];
    if (lv === UNASSIGNED || rv === UNASSIGNED) {
      return ConstraintResult.Unknown;
    }
    if (lv >= this.leftValues.length || rv >= this.rightValues.length) {
      return ConstraintResult.False;
    }
    return this.leftValues[lv] !== this.rightValues[rv]
      ? ConstraintResult.True
      : ConstraintResult.False;
  }
}
