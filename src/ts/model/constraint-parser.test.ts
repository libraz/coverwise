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

    // Prefix-NOT nesting uses the same 128-level bound as parenthesis nesting.
    const negated = (depth: number) => `${'NOT '.repeat(depth)}p=x`;
    expect(parseConstraint(negated(128), params).error.code).toBe(ErrorCode.Ok);
    expect(parseConstraint(negated(129), params).error.message).toContain('depth limit');

    // A flat expression with many operators but shallow nesting is accepted:
    // the depth guard measures real nesting, not the total operator count.
    const flat = `p=x${' OR p=x'.repeat(300)}`;
    expect(parseConstraint(flat, params).error.code).toBe(ErrorCode.Ok);
  });

  it('enforces the AST node boundary while balancing logical chains', () => {
    const params = [new Parameter('p', ['x'])];
    const conjunction = (clauses: number) => new Array(clauses).fill('p=x').join(' AND ');
    const atLimit = parseConstraint(conjunction(512), params);
    expect(atLimit.error.code).toBe(ErrorCode.Ok);
    expect(atLimit.constraint?.evaluate([0])).toBe(ConstraintResult.True);
    expect(parseConstraint(conjunction(513), params).error.message).toContain('node limit');
  });

  it('accepts a dash-prefixed non-numeric value on the RHS of = / !=', () => {
    // Regression: `flag = -on` must be read as the value '-on', the same way it
    // is accepted inside an IN set, not rejected as an invalid decimal literal.
    const params = [new Parameter('flag', ['-on', '-off', '0'])];
    const eq = parseConstraint('flag = -on', params);
    expect(eq.error.code).toBe(ErrorCode.Ok);
    expect(eq.constraint?.evaluate([0])).toBe(ConstraintResult.True); // -on
    expect(eq.constraint?.evaluate([1])).toBe(ConstraintResult.False); // -off
    expect(parseConstraint('flag IN {-on, -off}', params).error.code).toBe(ErrorCode.Ok);
  });

  it('treats a quoted RHS as a value even when it collides with a parameter name', () => {
    // Regression: `speed != "mode"` must not be silently reinterpreted as the
    // param-to-param comparison `speed != mode`. "mode" is not a value of speed,
    // so the quoted form is a value-lookup error; the unquoted form is valid.
    const params = [new Parameter('speed', ['fast', 'slow']), new Parameter('mode', ['a', 'b'])];
    expect(parseConstraint('speed != "mode"', params).error.code).not.toBe(ErrorCode.Ok);
    expect(parseConstraint('speed != mode', params).error.code).toBe(ErrorCode.Ok);
  });

  it('compares two parameters case-insensitively by default, honoring caseSensitive', () => {
    // Regression: param-to-param comparison previously compared value strings
    // byte-strict even when name/value matching is case-insensitive by default,
    // contradicting the documented case-insensitive behavior.
    const params = [new Parameter('a', ['X', 'Y']), new Parameter('b', ['x', 'z'])];

    const eq = parseConstraint('a = b', params);
    expect(eq.error.code).toBe(ErrorCode.Ok);
    expect(eq.constraint?.evaluate([0, 0])).toBe(ConstraintResult.True); // 'X' == 'x'
    expect(eq.constraint?.evaluate([1, 0])).toBe(ConstraintResult.False); // 'Y' != 'x'

    const neq = parseConstraint('a != b', params);
    expect(neq.constraint?.evaluate([0, 0])).toBe(ConstraintResult.False); // equal -> != false

    // With caseSensitive, 'X' and 'x' are distinct.
    const eqCs = parseConstraint('a = b', params, { caseSensitive: true });
    expect(eqCs.constraint?.evaluate([0, 0])).toBe(ConstraintResult.False);
  });

  it('reads a LIKE pattern starting with a digit as a pattern', () => {
    // Regression: a version glob like `1.*` must be a pattern, not a decimal
    // literal that then chokes on the glob character.
    const params = [new Parameter('version', ['1.0', '1.5', '2.0'])];
    const result = parseConstraint('version LIKE 1.*', params);
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.constraint?.evaluate([0])).toBe(ConstraintResult.True); // 1.0
    expect(result.constraint?.evaluate([1])).toBe(ConstraintResult.True); // 1.5
    expect(result.constraint?.evaluate([2])).toBe(ConstraintResult.False); // 2.0
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
