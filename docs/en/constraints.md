# Constraint Syntax

Constraints define invalid parameter combinations that coverwise excludes during generation. The same constraint strings are available in JavaScript, the C++ API, and CLI JSON input; the fluent builder below is JavaScript-only.

All TypeScript samples below are fragments. In a runnable module, use this setup
first; expression-only samples belong in a `constraints` array.

```typescript
import { Coverwise, when, not, allOf, anyOf } from '@libraz/coverwise';

const cw = await Coverwise.create();
```

## Basic Syntax

### Equality

```
IF os = Windows THEN browser != Safari
```

Parameter names and values are **case-insensitive** for matching. Resolution
folds ASCII case, so a model may not contain two parameter names that differ only
by case, nor two values or aliases of one parameter that differ only by case:
`os = WINDOWS` would then have no single answer. Such a model is rejected as
invalid input before generation starts.

### Quoted Values

A bare token may contain ASCII letters and digits, `_`, `-`, `.`, and any
non-ASCII character. A value built from anything else — a space, `+`, `%`, `@`,
`/`, `(`, `)` — must be written as a quoted string, in double or single quotes:

```
IF os = macOS THEN filesystem = "HFS+"
IF language = "C++" THEN build_system != 'make (BSD)'
IF release = "1.0 (beta)" THEN channel = preview
```

Inside a quoted string, `\"` escapes the quote character and `\\` escapes a
backslash. A quoted token is always a literal value: it is never read as a
keyword and never as a parameter name, so a value spelled `AND`, or one that
collides with a parameter name, is unambiguous once quoted.

The JavaScript builder quotes on your behalf — `when('filesystem').eq('HFS+')`
emits `filesystem = "HFS+"` — so quoting is a concern only for hand-written
constraint strings.

### IF / THEN / ELSE

```
IF os = macOS THEN browser = Safari OR browser = Chrome
IF os = macOS THEN browser = Safari ELSE browser != Safari
```

`ELSE` is optional.

### IMPLIES

`A IMPLIES B` states the same rule as `IF A THEN B`:

```
os = Linux IMPLIES arch != arm32
```

`IMPLIES` binds more loosely than every other operator, and an expression holds
at most one of them; write `IF` / `THEN` with parentheses for anything more
deeply nested.

### Logical Operators

```
IF os = Windows AND device = phone THEN browser = Edge
IF os = macOS OR os = iOS THEN browser = Safari
IF NOT os = Linux THEN arch = x64 OR arch = arm64
```

Precedence: `NOT` > `AND` > `OR` > `IMPLIES`. Use parentheses to override:

```
IF (os = Windows OR os = Linux) AND device = desktop THEN browser != Safari
```

### Relational Operators

For numeric values:

```
IF age >= 18 THEN plan != child
IF price < 0 THEN status = error
IF count > 100 THEN mode = batch
IF priority <= 3 THEN queue = high
```

Supported operators: `=`, `!=`, `<`, `<=`, `>`, `>=`.

### IN Operator

Match against a set of values:

```
IF os IN {Windows, macOS} THEN arch != arm32
IF browser IN {Chrome, Edge, Chromium} THEN engine = blink
```

### LIKE Operator

Pattern matching with wildcards:

```
IF browser LIKE Chrome* THEN engine = blink
IF version LIKE *.0.0 THEN is_major = true
IF code LIKE v?.0 THEN generation = first
```

`*` matches any sequence of characters, including the empty sequence; `?` matches
exactly one character. Both count Unicode codepoints rather than bytes, and both
keep their wildcard meaning inside a quoted pattern — there is no escape for a
literal `*` or `?`, so a value that really contains one has to be compared with
`=` instead. A pattern with no wildcard is an ordinary equality test.

### Parameter Comparison

Compare two parameters directly:

```
IF source = target THEN mode = copy
IF input_format != output_format THEN convert = true
```

## Keyword and Wildcard Reference

| Keyword | Role |
|---------|------|
| `IF` | Opens a conditional constraint. |
| `THEN` | Introduces the consequence of an `IF`. |
| `ELSE` | Optional alternative branch of an `IF`. |
| `IMPLIES` | Infix form of `IF ... THEN ...`. |
| `AND` | Conjunction. |
| `OR` | Disjunction. |
| `NOT` | Negation. |
| `IN` | Membership in a set literal. |
| `LIKE` | Glob pattern match. |

Keywords are case-insensitive; quote a value that is spelled like one.

| Wildcard | Matches |
|----------|---------|
| `*` | Any sequence of characters, including the empty sequence. |
| `?` | Exactly one character. |

## Combining Constraints

Pass multiple constraints as an array. All constraints must be satisfied simultaneously:

```typescript
cw.generate({
  parameters: [/* ... */],
  constraints: [
    'IF os = Windows THEN browser != Safari',
    'IF os = macOS THEN browser != IE',
    'IF device = phone THEN os IN {iOS, Android}',
  ],
});
```

## Unconditional Constraints

Omit `IF` for constraints that always apply. The expression itself is the rule,
and every generated test case must satisfy it:

```
browser != IE
os = Windows OR os = macOS
```

The grammar has no boolean literal, so an always-true antecedent cannot be
spelled out — drop the `IF` clause instead of writing one.

## Complex Examples

### Mutual Exclusion

```
IF os = iOS THEN browser = Safari
IF browser = Safari THEN os = macOS OR os = iOS
```

### Platform-Specific Features

```
IF os = Windows THEN filesystem IN {NTFS, FAT32}
IF os = macOS THEN filesystem IN {APFS, "HFS+"}
IF os = Linux THEN filesystem IN {ext4, btrfs, xfs}
```

### Multi-Condition

```
IF os = Windows AND browser = Chrome AND arch = arm64 THEN mode = compatibility
IF (os = iOS OR os = Android) AND screen_size < 7 THEN device = phone
```

## Constraint Errors

If constraints make certain tuples impossible, coverwise handles this gracefully:

- Tuples that cannot occur in any valid complete test case are excluded from the coverage universe
- Constraint-violating tuples therefore do not appear in `uncovered`
- If a `maxTests` limit prevents full coverage, `uncovered` contains only remaining required tuples

If constraints are contradictory (no valid combination exists), generation returns an error with code `CONSTRAINT_ERROR`.

## Constraint Builder (JavaScript)

Build constraints programmatically with the fluent API. Builder objects produce valid constraint strings via `toString()`.

Quoting is decided per method, because the position an operand lands in decides
how the parser reads it. You never escape a value yourself; what changes is
whether the value is read as a value at all.

- `eq()` and `ne()` always quote a string operand. A bare token there would be
  resolved as a parameter reference whenever a parameter bears that name, which
  would silently turn a value comparison into a parameter-to-parameter one.
- `in()` and `like()` quote only when the value cannot survive as one bare
  token. `in('staging', 'prod')` emits `env IN {staging, prod}` and
  `like('chrome*')` emits `browser LIKE chrome*`; the `*` and `?` wildcards keep
  their meaning either way.
- `gt()`, `gte()`, `lt()` and `lte()` take a number as the value to compare
  against. A **string** operand there is a parameter name, emitted bare, and it
  is refused outright when it cannot be written as one bare token — so
  `when('status').lt('ok')` compares `status` against a parameter called `ok`
  rather than against the value `ok`.

### Basic Comparisons

```typescript
when('os').eq('Windows')           // os = "Windows"
when('browser').ne('Safari')       // browser != "Safari"
when('version').gt(3)              // version > 3
when('version').gte(10)            // version >= 10
when('priority').lt(5)             // priority < 5
when('priority').lte(1)            // priority <= 1
```

### IN and LIKE

```typescript
when('env').in('staging', 'prod')  // env IN {staging, prod}
when('browser').like('chrome*')    // browser LIKE chrome*
when('code').like('v?.0')          // code LIKE v?.0
```

### Parameter-to-Parameter

```typescript
when('start_date').lt('end_date')  // start_date < end_date
```

### IF / THEN / ELSE and IMPLIES

```typescript
when('os').eq('Windows')
  .then(when('browser').ne('Safari'))
// IF os = "Windows" THEN browser != "Safari"

when('os').eq('mac')
  .then(when('browser').ne('ie'))
  .else(when('arch').ne('arm'))
// IF os = "mac" THEN browser != "ie" ELSE arch != "arm"

when('os').eq('linux')
  .implies(when('arch').ne('arm'))
// os = "linux" IMPLIES arch != "arm"
```

`else()` is available only on the result of `then()`, because the grammar gives
no reading to a second `ELSE`.

### Logical Composition

```typescript
// AND
allOf(when('os').eq('win'), when('arch').eq('x64'))
when('os').eq('win').and(when('arch').eq('x64'))
// os = "win" AND arch = "x64"

// OR
anyOf(when('os').eq('win'), when('os').eq('linux'))
when('os').eq('win').or(when('os').eq('linux'))
// os = "win" OR os = "linux"

// NOT
not(allOf(when('os').eq('win'), when('browser').eq('safari')))
// NOT (os = "win" AND browser = "safari")
```

`allOf()` and `anyOf()` fold any number of conditions with `and()` and `or()`
respectively, and reject an empty argument list.

### Method Reference

| Method | Emits |
|--------|-------|
| `eq(value)` | `param = "value"` |
| `ne(value)` | `param != "value"` |
| `gt(n)` | `param > n` |
| `gte(n)` | `param >= n` |
| `lt(n)` | `param < n` |
| `lte(n)` | `param <= n` |
| `in(...values)` | `param IN {…}` |
| `like(pattern)` | `param LIKE pattern` |
| `and(other)` | `… AND …` |
| `or(other)` | `… OR …` |
| `then(consequence)` | `IF … THEN …` |
| `implies(consequence)` | `… IMPLIES …` |
| `else(alternative)` | `… ELSE …`, available only after `then()` |
| `toString()` | The constraint string to pass to `generate()`. |

### Using with generate()

Builder objects must be converted to strings with `.toString()`:

```typescript
cw.generate({
  parameters: [/* ... */],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    when('device').eq('phone').then(when('os').in('iOS', 'Android')).toString(),
  ],
});
```

String constraints and builder constraints can be mixed freely.
