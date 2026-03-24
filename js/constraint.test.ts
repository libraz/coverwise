import { describe, expect, it } from 'vitest';
import { allOf, anyOf, not, when } from './constraint';

// ---------------------------------------------------------------------------
// Basic operators
// ---------------------------------------------------------------------------

describe('when().eq()', () => {
  it('simple string value', () => {
    expect(when('os').eq('Windows').toString()).toBe('os = Windows');
  });

  it('number value is unquoted', () => {
    expect(when('version').eq(3).toString()).toBe('version = 3');
  });

  it('boolean true is unquoted', () => {
    expect(when('debug').eq(true).toString()).toBe('debug = true');
  });

  it('boolean false is unquoted', () => {
    expect(when('debug').eq(false).toString()).toBe('debug = false');
  });

  it('value with spaces is quoted', () => {
    expect(when('os').eq('Windows 11').toString()).toBe('os = "Windows 11"');
  });

  it('value with = is quoted', () => {
    expect(when('expr').eq('a=b').toString()).toBe('expr = "a=b"');
  });

  it('value with parentheses is quoted', () => {
    expect(when('name').eq('foo(bar)').toString()).toBe('name = "foo(bar)"');
  });

  it('value with braces is quoted', () => {
    expect(when('json').eq('{x}').toString()).toBe('json = "{x}"');
  });

  it('value with commas is quoted', () => {
    expect(when('csv').eq('a,b').toString()).toBe('csv = "a,b"');
  });
});

describe('when().ne()', () => {
  it('simple string value', () => {
    expect(when('browser').ne('Safari').toString()).toBe('browser != Safari');
  });

  it('number value is unquoted', () => {
    expect(when('count').ne(0).toString()).toBe('count != 0');
  });
});

describe('relational operators', () => {
  it('gt with number', () => {
    expect(when('version').gt(3).toString()).toBe('version > 3');
  });

  it('gte with number', () => {
    expect(when('version').gte(10).toString()).toBe('version >= 10');
  });

  it('lt with number', () => {
    expect(when('priority').lt(5).toString()).toBe('priority < 5');
  });

  it('lte with number', () => {
    expect(when('priority').lte(1).toString()).toBe('priority <= 1');
  });

  it('gt with negative number', () => {
    expect(when('temp').gt(-10).toString()).toBe('temp > -10');
  });

  it('lt with float', () => {
    expect(when('ratio').lt(0.5).toString()).toBe('ratio < 0.5');
  });

  it('gt with zero', () => {
    expect(when('count').gt(0).toString()).toBe('count > 0');
  });

  it('param-to-param comparison (string arg = param name)', () => {
    expect(when('start_date').lt('end_date').toString()).toBe('start_date < end_date');
  });

  it('param-to-param gte', () => {
    expect(when('max').gte('min').toString()).toBe('max >= min');
  });
});

describe('when().in()', () => {
  it('string values', () => {
    expect(when('env').in('staging', 'prod').toString()).toBe('env IN {staging, prod}');
  });

  it('single value', () => {
    expect(when('os').in('Windows').toString()).toBe('os IN {Windows}');
  });

  it('mixed types', () => {
    expect(when('x').in('a', 1, true).toString()).toBe('x IN {a, 1, true}');
  });

  it('values with spaces are quoted', () => {
    expect(when('os').in('Windows 11', 'macOS Sonoma').toString()).toBe(
      'os IN {"Windows 11", "macOS Sonoma"}',
    );
  });
});

describe('when().like()', () => {
  it('glob pattern', () => {
    expect(when('browser').like('chrome*').toString()).toBe('browser LIKE chrome*');
  });

  it('single-char wildcard', () => {
    expect(when('code').like('v?.0').toString()).toBe('code LIKE v?.0');
  });

  it('pattern with no wildcards', () => {
    expect(when('name').like('exact').toString()).toBe('name LIKE exact');
  });
});

// ---------------------------------------------------------------------------
// Logical composition
// ---------------------------------------------------------------------------

describe('and()', () => {
  it('two conditions', () => {
    const c = when('os').eq('win').and(when('browser').eq('chrome'));
    expect(c.toString()).toBe('os = win AND browser = chrome');
  });

  it('three chained conditions', () => {
    const c = when('a').eq('1').and(when('b').eq('2')).and(when('c').eq('3'));
    expect(c.toString()).toBe('a = 1 AND b = 2 AND c = 3');
  });

  it('wraps OR operands in parens for precedence', () => {
    const orCond = when('a').eq('1').or(when('b').eq('2'));
    const result = orCond.and(when('c').eq('3'));
    expect(result.toString()).toBe('(a = 1 OR b = 2) AND c = 3');
  });
});

describe('or()', () => {
  it('two conditions', () => {
    const c = when('os').eq('win').or(when('os').eq('linux'));
    expect(c.toString()).toBe('os = win OR os = linux');
  });

  it('three chained conditions (left side wrapped)', () => {
    const c = when('a').eq('1').or(when('b').eq('2')).or(when('c').eq('3'));
    // or() wraps left operand containing OR in parens for unambiguous grouping
    expect(c.toString()).toBe('(a = 1 OR b = 2) OR c = 3');
  });
});

describe('not()', () => {
  it('simple condition', () => {
    expect(not(when('os').eq('win')).toString()).toBe('NOT (os = win)');
  });

  it('compound condition', () => {
    expect(not(allOf(when('a').eq('1'), when('b').eq('2'))).toString()).toBe(
      'NOT (a = 1 AND b = 2)',
    );
  });

  it('double negation', () => {
    expect(not(not(when('x').eq('y'))).toString()).toBe('NOT (NOT (x = y))');
  });
});

describe('allOf()', () => {
  it('single condition passes through', () => {
    const c = allOf(when('os').eq('win'));
    expect(c.toString()).toBe('os = win');
  });

  it('multiple conditions', () => {
    const c = allOf(when('a').eq('1'), when('b').eq('2'), when('c').eq('3'));
    expect(c.toString()).toBe('a = 1 AND b = 2 AND c = 3');
  });

  it('wraps OR sub-expressions', () => {
    const c = allOf(when('a').eq('1').or(when('a').eq('2')), when('b').eq('3'));
    expect(c.toString()).toBe('(a = 1 OR a = 2) AND b = 3');
  });

  it('throws on empty', () => {
    expect(() => allOf()).toThrow('at least one');
  });
});

describe('anyOf()', () => {
  it('single condition passes through', () => {
    const c = anyOf(when('os').eq('win'));
    expect(c.toString()).toBe('os = win');
  });

  it('multiple conditions', () => {
    const c = anyOf(when('a').eq('1'), when('b').eq('2'), when('c').eq('3'));
    expect(c.toString()).toBe('a = 1 OR b = 2 OR c = 3');
  });

  it('throws on empty', () => {
    expect(() => anyOf()).toThrow('at least one');
  });
});

// ---------------------------------------------------------------------------
// IF / THEN / ELSE / IMPLIES
// ---------------------------------------------------------------------------

describe('then()', () => {
  it('IF...THEN', () => {
    const c = when('os').eq('Windows').then(when('browser').ne('Safari'));
    expect(c.toString()).toBe('IF os = Windows THEN browser != Safari');
  });

  it('IF compound THEN compound', () => {
    const c = allOf(when('os').eq('win'), when('arch').eq('x64')).then(when('browser').ne('ie'));
    expect(c.toString()).toBe('IF os = win AND arch = x64 THEN browser != ie');
  });
});

describe('else()', () => {
  it('IF...THEN...ELSE', () => {
    const c = when('os').eq('mac').then(when('browser').ne('ie')).else(when('arch').ne('arm'));
    expect(c.toString()).toBe('IF os = mac THEN browser != ie ELSE arch != arm');
  });
});

describe('implies()', () => {
  it('basic implies', () => {
    const c = when('os').eq('linux').implies(when('arch').ne('arm'));
    expect(c.toString()).toBe('os = linux IMPLIES arch != arm');
  });
});

// ---------------------------------------------------------------------------
// Keyword collision
// ---------------------------------------------------------------------------

describe('keyword values', () => {
  it('value matching keyword IF is quoted', () => {
    expect(when('mode').eq('IF').toString()).toBe('mode = "IF"');
  });

  it('value matching keyword AND (case-insensitive) is quoted', () => {
    expect(when('op').eq('and').toString()).toBe('op = "and"');
  });

  it('value matching keyword OR is quoted', () => {
    expect(when('op').eq('OR').toString()).toBe('op = "OR"');
  });

  it('value matching keyword NOT is quoted', () => {
    expect(when('flag').eq('not').toString()).toBe('flag = "not"');
  });

  it('value matching keyword IN is quoted', () => {
    expect(when('x').eq('IN').toString()).toBe('x = "IN"');
  });

  it('value matching keyword LIKE is quoted', () => {
    expect(when('x').eq('LIKE').toString()).toBe('x = "LIKE"');
  });

  it('value matching keyword IMPLIES is quoted', () => {
    expect(when('x').eq('implies').toString()).toBe('x = "implies"');
  });

  it('value matching keyword THEN is quoted', () => {
    expect(when('x').eq('Then').toString()).toBe('x = "Then"');
  });

  it('value matching keyword ELSE is quoted', () => {
    expect(when('x').eq('ELSE').toString()).toBe('x = "ELSE"');
  });

  it('keyword in set is quoted', () => {
    expect(when('x').in('AND', 'normal').toString()).toBe('x IN {"AND", normal}');
  });
});

// ---------------------------------------------------------------------------
// Special characters & edge cases
// ---------------------------------------------------------------------------

describe('special characters in values', () => {
  it('exclamation mark triggers quoting', () => {
    expect(when('msg').eq('hello!').toString()).toBe('msg = "hello!"');
  });

  it('angle brackets trigger quoting', () => {
    expect(when('tag').eq('<b>').toString()).toBe('tag = "<b>"');
  });

  it('empty string', () => {
    expect(when('x').eq('').toString()).toBe('x = ');
  });

  it('value is a single space', () => {
    expect(when('x').eq(' ').toString()).toBe('x = " "');
  });

  it('tab character triggers quoting', () => {
    expect(when('x').eq('a\tb').toString()).toBe('x = "a\tb"');
  });

  it('newline character triggers quoting', () => {
    expect(when('x').eq('a\nb').toString()).toBe('x = "a\nb"');
  });

  it('carriage return triggers quoting', () => {
    expect(when('x').eq('a\rb').toString()).toBe('x = "a\rb"');
  });

  it('null byte in value', () => {
    expect(when('x').eq('a\0b').toString()).toBe('x = a\0b');
  });
});

describe('unicode and emoji', () => {
  it('emoji value — no special chars, no quoting needed', () => {
    expect(when('icon').eq('🚀').toString()).toBe('icon = 🚀');
  });

  it('emoji with spaces is quoted', () => {
    expect(when('label').eq('🚀 launch').toString()).toBe('label = "🚀 launch"');
  });

  it('CJK characters', () => {
    expect(when('lang').eq('日本語').toString()).toBe('lang = 日本語');
  });

  it('mixed unicode and operators', () => {
    expect(when('name').eq('café=latte').toString()).toBe('name = "café=latte"');
  });

  it('emoji parameter name', () => {
    expect(when('🎯').eq('hit').toString()).toBe('🎯 = hit');
  });

  it('surrogate pair emoji (multi-codepoint)', () => {
    expect(when('face').eq('👨‍👩‍👧‍👦').toString()).toBe('face = 👨‍👩‍👧‍👦');
  });

  it('flag emoji', () => {
    expect(when('country').eq('🇯🇵').toString()).toBe('country = 🇯🇵');
  });
});

describe('escape and quoting edge cases', () => {
  it('value containing double quotes (no escaping — raw passthrough)', () => {
    const result = when('x').eq('say "hello"').toString();
    // Contains space → quoted, but inner quotes are not escaped
    expect(result).toBe('x = "say "hello""');
  });

  it('value containing backslash', () => {
    expect(when('path').eq('C:\\Users').toString()).toBe('path = C:\\Users');
  });

  it('value containing backslash and space', () => {
    expect(when('path').eq('C:\\Program Files').toString()).toBe('path = "C:\\Program Files"');
  });

  it('value is only special chars', () => {
    expect(when('x').eq('!=').toString()).toBe('x = "!="');
  });

  it('value with consecutive spaces', () => {
    expect(when('x').eq('a  b').toString()).toBe('x = "a  b"');
  });
});

// ---------------------------------------------------------------------------
// Number edge cases
// ---------------------------------------------------------------------------

describe('numeric edge cases', () => {
  it('eq with 0', () => {
    expect(when('n').eq(0).toString()).toBe('n = 0');
  });

  it('eq with negative number', () => {
    expect(when('n').eq(-1).toString()).toBe('n = -1');
  });

  it('eq with float', () => {
    expect(when('n').eq(3.14).toString()).toBe('n = 3.14');
  });

  it('gt with Infinity', () => {
    expect(when('n').gt(Number.POSITIVE_INFINITY).toString()).toBe('n > Infinity');
  });

  it('lt with NaN', () => {
    expect(when('n').lt(Number.NaN).toString()).toBe('n < NaN');
  });

  it('in with mixed numbers', () => {
    expect(when('n').in(0, -1, 3.14).toString()).toBe('n IN {0, -1, 3.14}');
  });

  it('ne with boolean false', () => {
    expect(when('flag').ne(false).toString()).toBe('flag != false');
  });
});

// ---------------------------------------------------------------------------
// Complex composition
// ---------------------------------------------------------------------------

describe('complex expressions', () => {
  it('nested AND/OR with correct precedence', () => {
    const c = anyOf(when('os').eq('win'), when('os').eq('linux')).and(when('browser').eq('chrome'));
    expect(c.toString()).toBe('(os = win OR os = linux) AND browser = chrome');
  });

  it('deeply nested NOT', () => {
    const c = not(not(not(when('x').eq('1'))));
    expect(c.toString()).toBe('NOT (NOT (NOT (x = 1)))');
  });

  it('IF with complex condition and ELSE', () => {
    const c = allOf(when('os').eq('win'), when('arch').eq('x64'))
      .then(anyOf(when('browser').ne('ie'), when('version').gt(11)))
      .else(when('fallback').eq('true'));
    expect(c.toString()).toBe(
      'IF os = win AND arch = x64 THEN browser != ie OR version > 11 ELSE fallback = true',
    );
  });

  it('chained implies', () => {
    const c = when('a').eq('1').implies(when('b').eq('2'));
    expect(c.toString()).toBe('a = 1 IMPLIES b = 2');
  });

  it('allOf with anyOf children', () => {
    const c = allOf(
      anyOf(when('a').eq('1'), when('a').eq('2')),
      anyOf(when('b').eq('3'), when('b').eq('4')),
    );
    expect(c.toString()).toBe('(a = 1 OR a = 2) AND (b = 3 OR b = 4)');
  });

  it('anyOf with allOf children (no extra parens needed)', () => {
    const c = anyOf(
      allOf(when('a').eq('1'), when('b').eq('2')),
      allOf(when('c').eq('3'), when('d').eq('4')),
    );
    expect(c.toString()).toBe('a = 1 AND b = 2 OR c = 3 AND d = 4');
  });
});

// ---------------------------------------------------------------------------
// Immutability
// ---------------------------------------------------------------------------

describe('immutability', () => {
  it('and() does not mutate original', () => {
    const a = when('os').eq('win');
    const b = a.and(when('browser').eq('chrome'));
    expect(a.toString()).toBe('os = win');
    expect(b.toString()).toBe('os = win AND browser = chrome');
  });

  it('or() does not mutate original', () => {
    const a = when('os').eq('win');
    const b = a.or(when('os').eq('mac'));
    expect(a.toString()).toBe('os = win');
    expect(b.toString()).toBe('os = win OR os = mac');
  });

  it('then() does not mutate condition', () => {
    const cond = when('os').eq('win');
    const constraint = cond.then(when('browser').ne('ie'));
    expect(cond.toString()).toBe('os = win');
    expect(constraint.toString()).toBe('IF os = win THEN browser != ie');
  });

  it('else() does not mutate constraint', () => {
    const c1 = when('os').eq('win').then(when('browser').ne('ie'));
    const c2 = c1.else(when('arch').eq('arm'));
    expect(c1.toString()).toBe('IF os = win THEN browser != ie');
    expect(c2.toString()).toBe('IF os = win THEN browser != ie ELSE arch = arm');
  });
});

// ---------------------------------------------------------------------------
// toString() interop with constraints: string[]
// ---------------------------------------------------------------------------

describe('string[] interop', () => {
  it('constraint auto-converts in template literal', () => {
    const c = when('os').eq('win').then(when('browser').ne('ie'));
    expect(`${c}`).toBe('IF os = win THEN browser != ie');
  });

  it('condition auto-converts in template literal', () => {
    const c = when('os').eq('win');
    expect(`${c}`).toBe('os = win');
  });

  it('constraint in array works with String()', () => {
    const c = when('os').eq('win').then(when('browser').ne('ie'));
    expect(String(c)).toBe('IF os = win THEN browser != ie');
  });
});
