import { describe, expect, it } from 'vitest';
import { ConstraintResult } from './constraint-ast.js';
import { parseConstraint } from './constraint-parser.js';
import { ErrorCode } from './error.js';
import { Parameter } from './parameter.js';

describe('constraint parser safety limits', () => {
  it('uses strict finite decimal grammar', () => {
    const params = [new Parameter('n', ['0', '0.5', '1000'])];

    const leadingDot = parseConstraint('n >= .5', params);
    expect(leadingDot.error.code).toBe(ErrorCode.Ok);
    expect(leadingDot.constraint?.evaluate([1])).toBe(ConstraintResult.True);

    const exponent = parseConstraint('n < 1e3', params);
    expect(exponent.error.code).toBe(ErrorCode.Ok);
    expect(exponent.constraint?.evaluate([1])).toBe(ConstraintResult.True);

    for (const expression of ['n > 1..2', 'n > 1e', 'n > 1e309', 'n > 1e-999']) {
      expect(parseConstraint(expression, params).error.code, expression).not.toBe(ErrorCode.Ok);
    }
  });

  it('enforces the UTF-8 byte length boundary', () => {
    const name = 'a'.repeat(65_534);
    const params = [new Parameter(name, ['x'])];
    expect(parseConstraint(`${name}=x`, params).error.code).toBe(ErrorCode.Ok);
    expect(parseConstraint(`${name} =x`, params).error.message).toContain('byte limit');
  });

  it('enforces the token limit', () => {
    const params = [new Parameter('p', ['x'])];
    const expression = `p IN {${new Array(2050).fill('x').join(',')}}`;
    expect(parseConstraint(expression, params).error.message).toContain('token limit');
  });

  it('enforces nesting and logical AST depth boundaries', () => {
    const params = [new Parameter('p', ['x'])];
    const nested = (depth: number) => `${'('.repeat(depth)}p=x${')'.repeat(depth)}`;
    expect(parseConstraint(nested(128), params).error.code).toBe(ErrorCode.Ok);
    expect(parseConstraint(nested(129), params).error.message).toContain('depth limit');

    const negated = (depth: number) => `${'NOT '.repeat(depth)}p=x`;
    expect(parseConstraint(negated(127), params).error.code).toBe(ErrorCode.Ok);
    expect(parseConstraint(negated(128), params).error.message).toContain('AST depth limit');
  });

  it('enforces the AST node boundary while balancing logical chains', () => {
    const params = [new Parameter('p', ['x'])];
    const conjunction = (clauses: number) => new Array(clauses).fill('p=x').join(' AND ');
    const atLimit = parseConstraint(conjunction(512), params);
    expect(atLimit.error.code).toBe(ErrorCode.Ok);
    expect(atLimit.constraint?.evaluate([0])).toBe(ConstraintResult.True);
    expect(parseConstraint(conjunction(513), params).error.message).toContain('node limit');
  });

  it('never throws for a deterministic malformed-input corpus', () => {
    const params = [new Parameter('p', ['x', 'y'])];
    const alphabet = `abcXYZ019.=!<>(){},?*+-_'" `;
    let state = 0xc0ffee;
    for (let sample = 0; sample < 2000; sample++) {
      state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
      const length = state % 257;
      let expression = '';
      for (let i = 0; i < length; i++) {
        state = (Math.imul(state, 1664525) + 1013904223) >>> 0;
        expression += alphabet[state % alphabet.length];
      }
      expect(() => parseConstraint(expression, params), `sample=${sample}`).not.toThrow();
    }
  });
});
