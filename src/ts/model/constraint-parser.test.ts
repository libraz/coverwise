import { describe, expect, it } from 'vitest';
import { CONSTRAINT_CORPUS } from '../../../tests/util/constraint-error-corpus.js';
import { NUMERIC_PARSE_CORPUS } from '../../../tests/util/numeric-parse-corpus.js';
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

  it('treats a quoted relational RHS as a literal, not a parameter reference', () => {
    // Whether a right-hand identifier resolves as a parameter reference is
    // decided by the quoting alone, and the relational operators decide it the
    // same way = and != do. A quoted operand that happens to spell a parameter
    // name parses to whatever it would parse to if no parameter carried that
    // name -- here a non-numeric literal, which no relational operator accepts.
    const withCollision = [
      new Parameter('threshold', ['1', '5', '10']),
      new Parameter('limit', ['1', '5', '10']),
    ];
    const withoutCollision = [
      new Parameter('threshold', ['1', '5', '10']),
      new Parameter('ceiling', ['1', '5', '10']),
    ];

    for (const op of ['<', '<=', '>', '>=']) {
      const quoted = `threshold ${op} "limit"`;
      const colliding = parseConstraint(quoted, withCollision);
      const plain = parseConstraint(quoted, withoutCollision);
      expect(colliding.error.code).not.toBe(ErrorCode.Ok);
      expect(colliding.error.code).toBe(plain.error.code);
      expect(colliding.error.message).toBe(plain.error.message);

      // The unquoted form is still a parameter-to-parameter comparison.
      expect(parseConstraint(`threshold ${op} limit`, withCollision).error.code).toBe(ErrorCode.Ok);
    }
  });

  it('still reads a quoted relational RHS that spells a number as a number', () => {
    const params = [new Parameter('threshold', ['1', '5', '10']), new Parameter('7', ['a', 'b'])];

    const quoted = parseConstraint('threshold < "7"', params);
    expect(quoted.error.code).toBe(ErrorCode.Ok);
    expect(quoted.constraint?.evaluate([0, 0])).toBe(ConstraintResult.True);
    expect(quoted.constraint?.evaluate([2, 0])).toBe(ConstraintResult.False);
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

  // The corpus is shared with the C++ tests, which read the same file, so the
  // literal path of every surface accepts and rejects the same decimals.
  it('classifies every decimal in the shared corpus the same way as the core', () => {
    const params = [new Parameter('n', ['0', '1'])];
    for (const numericCase of NUMERIC_PARSE_CORPUS) {
      const result = parseConstraint(`n > ${numericCase.text}`, params);
      if (numericCase.acceptsLiteral) {
        expect(result.error.code, numericCase.text).toBe(ErrorCode.Ok);
      } else {
        expect(result.error.code, numericCase.text).toBe(ErrorCode.ConstraintError);
        expect(result.error.message, numericCase.text).toContain('out-of-range decimal literal');
      }
    }
  });

  it('keeps subnormal literals and values apart from zero and from each other', () => {
    const params = [new Parameter('n', ['0', '5e-324', '1e-310'])];

    const positive = parseConstraint('n > 0', params);
    expect(positive.error.code).toBe(ErrorCode.Ok);
    expect(positive.constraint?.evaluate([0])).toBe(ConstraintResult.False);
    expect(positive.constraint?.evaluate([1])).toBe(ConstraintResult.True);
    expect(positive.constraint?.evaluate([2])).toBe(ConstraintResult.True);

    const belowLiteral = parseConstraint('n < 1e-310', params);
    expect(belowLiteral.error.code).toBe(ErrorCode.Ok);
    expect(belowLiteral.constraint?.evaluate([1])).toBe(ConstraintResult.True);
    expect(belowLiteral.constraint?.evaluate([2])).toBe(ConstraintResult.False);
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

  it('matches a LIKE pattern case-insensitively by default, honoring caseSensitive', () => {
    const params = [new Parameter('browser', ['Chrome', 'Chromium', 'Firefox'])];

    const result = parseConstraint('browser LIKE chrom*', params);
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.constraint?.evaluate([0])).toBe(ConstraintResult.True); // Chrome
    expect(result.constraint?.evaluate([1])).toBe(ConstraintResult.True); // Chromium
    expect(result.constraint?.evaluate([2])).toBe(ConstraintResult.False); // Firefox

    const sensitive = parseConstraint('browser LIKE chrom*', params, { caseSensitive: true });
    expect(sensitive.error.code).toBe(ErrorCode.Ok);
    expect(sensitive.constraint?.evaluate([0])).toBe(ConstraintResult.False);
    expect(sensitive.constraint?.evaluate([1])).toBe(ConstraintResult.False);

    // In case-sensitive mode the pattern still matches when the case agrees.
    const exact = parseConstraint('browser LIKE Chrom*', params, { caseSensitive: true });
    expect(exact.constraint?.evaluate([0])).toBe(ConstraintResult.True);
    expect(exact.constraint?.evaluate([2])).toBe(ConstraintResult.False);
  });

  it('folds LIKE patterns with the ASCII-only case policy', () => {
    // Non-ASCII characters keep comparing exactly, as they do for = / != / IN.
    const params = [new Parameter('label', ['CAFE', 'CAFÉ'])];

    const ascii = parseConstraint('label LIKE cafe*', params);
    expect(ascii.error.code).toBe(ErrorCode.Ok);
    expect(ascii.constraint?.evaluate([0])).toBe(ConstraintResult.True);

    const accented = parseConstraint('label LIKE café*', params);
    expect(accented.error.code).toBe(ErrorCode.Ok);
    expect(accented.constraint?.evaluate([1])).toBe(ConstraintResult.False);
  });

  it('treats a LIKE pattern without a wildcard as an equality test', () => {
    const browser = new Parameter('browser', ['chrome', 'firefox', 'safari']);
    browser.setAliases([['cr'], [], []]);
    const params = [browser];

    for (const expression of [
      'browser LIKE chrome',
      'browser LIKE CHROME',
      'browser LIKE cr',
      'browser LIKE CR',
    ]) {
      const result = parseConstraint(expression, params);
      expect(result.error.code, expression).toBe(ErrorCode.Ok);
      expect(result.constraint?.evaluate([0]), expression).toBe(ConstraintResult.True);
      expect(result.constraint?.evaluate([1]), expression).toBe(ConstraintResult.False);
    }

    const unknown = parseConstraint('browser LIKE xyz', params);
    expect(unknown.error.code).toBe(ErrorCode.ConstraintError);
    expect(unknown.error.message).toBe("Unknown value 'xyz' for parameter 'browser'");

    // A wildcard pattern keeps matching by glob, aliases excluded.
    const glob = parseConstraint('browser LIKE c*', params);
    expect(glob.error.code).toBe(ErrorCode.Ok);
    expect(glob.constraint?.evaluate([0])).toBe(ConstraintResult.True);
    expect(glob.constraint?.evaluate([1])).toBe(ConstraintResult.False);
  });

  it('counts a lone surrogate as one unit that a literal U+00A0 never matches', () => {
    const params = [new Parameter('text', ['\uD800', 'x', '\u00A0'])];

    const anyOne = parseConstraint('text LIKE ?', params);
    expect(anyOne.error.code).toBe(ErrorCode.Ok);
    for (let i = 0; i < 3; i++) {
      expect(anyOne.constraint?.evaluate([i]), `index ${i}`).toBe(ConstraintResult.True);
    }

    const nbsp = parseConstraint('text LIKE *\u00A0*', params);
    expect(nbsp.error.code).toBe(ErrorCode.Ok);
    expect(nbsp.constraint?.evaluate([0])).toBe(ConstraintResult.False);
    expect(nbsp.constraint?.evaluate([1])).toBe(ConstraintResult.False);
    expect(nbsp.constraint?.evaluate([2])).toBe(ConstraintResult.True);
  });

  it('builds many comparisons of the same wide parameters from shared keys', () => {
    const left = Array.from({ length: 16384 }, (_, i) => `v${i}`);
    const right = Array.from({ length: 16384 }, (_, i) => `v${i + 8192}`);
    const params = [new Parameter('p0', left), new Parameter('p1', right)];

    const expression = `p0 = p1${' OR p0 != p1'.repeat(499)}`;
    const result = parseConstraint(expression, params);
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.constraint?.evaluate([8192, 0])).toBe(ConstraintResult.True);
    expect(result.constraint?.evaluate([0, 0])).toBe(ConstraintResult.True);

    const onlyEqual = parseConstraint('p0 = p1 AND p0 = p1', params);
    expect(onlyEqual.constraint?.evaluate([8192, 0])).toBe(ConstraintResult.True);
    expect(onlyEqual.constraint?.evaluate([0, 0])).toBe(ConstraintResult.False);
  });

  // The corpus is shared with the C++ tests, which read the same file, so both
  // parsers accept and reject the same expressions with the same text.
  it('accepts and rejects every expression in the shared constraint corpus like the core', () => {
    const browser = new Parameter('browser', ['chrome', 'firefox', 'safari']);
    browser.setAliases([['cr'], [], []]);
    const params = [
      new Parameter('os', ['win', 'mac', 'linux']),
      browser,
      new Parameter('n', ['1', '2', '10']),
    ];
    for (const entry of CONSTRAINT_CORPUS) {
      const result = parseConstraint(entry.expression, params);
      if (entry.accepted) {
        expect(result.error.code, entry.expression).toBe(ErrorCode.Ok);
        expect(result.constraint, entry.expression).not.toBeNull();
      } else {
        expect(result.error.code, entry.expression).toBe(ErrorCode.ConstraintError);
        expect(result.error.message, entry.expression).toBe(entry.message);
        expect(result.error.detail, entry.expression).toBe(entry.detail);
        expect(result.constraint, entry.expression).toBeNull();
      }
    }
  });

  it('excludes the same combinations a LIKE constraint excludes in the C++ core', () => {
    const params = [
      new Parameter('browser', ['Chrome', 'Chromium', 'Firefox']),
      new Parameter('engine', ['blink', 'gecko']),
    ];

    const result = parseConstraint('IF browser LIKE chrom* THEN engine = blink', params);
    expect(result.error.code).toBe(ErrorCode.Ok);

    const excluded: string[] = [];
    for (let b = 0; b < params[0].values.length; b++) {
      for (let e = 0; e < params[1].values.length; e++) {
        if (result.constraint?.evaluate([b, e]) === ConstraintResult.False) {
          excluded.push(`${params[0].values[b]}/${params[1].values[e]}`);
        }
      }
    }

    expect(excluded).toEqual(['Chrome/gecko', 'Chromium/gecko']);
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
