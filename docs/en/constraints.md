# Constraint Syntax

Constraints define invalid parameter combinations that must never appear in generated tests. coverwise evaluates constraints during generation and prunes violating candidates.

## Basic Syntax

### Equality

```
IF os = Windows THEN browser != Safari
```

Parameter names and values are **case-insensitive** for matching.

### IF / THEN / ELSE

```
IF os = macOS THEN browser = Safari OR browser = Chrome
IF os = macOS THEN browser = Safari ELSE browser != Safari
```

`ELSE` is optional.

### Logical Operators

```
IF os = Windows AND device = phone THEN browser = Edge
IF os = macOS OR os = iOS THEN browser = Safari
IF NOT os = Linux THEN arch = x64 OR arch = arm64
```

Precedence: `NOT` > `AND` > `OR`. Use parentheses to override:

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
```

`*` matches any sequence of characters.

### Parameter Comparison

Compare two parameters directly:

```
IF source = target THEN mode = copy
IF input_format != output_format THEN convert = true
```

## Combining Constraints

Pass multiple constraints as an array. All constraints must be satisfied simultaneously:

```typescript
generate({
  parameters: [/* ... */],
  constraints: [
    'IF os = Windows THEN browser != Safari',
    'IF os = macOS THEN browser != IE',
    'IF device = phone THEN os IN {iOS, Android}',
  ],
});
```

## Unconditional Constraints

Omit `IF` for constraints that always apply:

```
browser != IE
os = Windows OR os = macOS
```

These are equivalent to:

```
IF true THEN browser != IE
```

## Complex Examples

### Mutual Exclusion

```
IF os = iOS THEN browser = Safari
IF browser = Safari THEN os = macOS OR os = iOS
```

### Platform-Specific Features

```
IF os = Windows THEN filesystem IN {NTFS, FAT32}
IF os = macOS THEN filesystem IN {APFS, HFS+}
IF os = Linux THEN filesystem IN {ext4, btrfs, xfs}
```

### Multi-Condition

```
IF os = Windows AND browser = Chrome AND arch = arm64 THEN mode = compatibility
IF (os = iOS OR os = Android) AND screen_size < 7 THEN device = phone
```

## Constraint Errors

If constraints make certain tuples impossible, coverwise handles this gracefully:

- Constraint-violating combinations are excluded from the coverage target
- The generator never produces constraint-violating test cases
- Uncovered tuples report whether they were excluded by constraints

If constraints are contradictory (no valid combination exists), generation returns an error with code `CONSTRAINT_ERROR`.

## Constraint Builder (JavaScript)

Build constraints programmatically with the fluent API. Builder objects produce valid constraint strings via `toString()`.

```typescript
import { when, not, allOf, anyOf } from '@libraz/coverwise';
```

### Basic Comparisons

```typescript
when('os').eq('Windows')           // os = Windows
when('browser').ne('Safari')       // browser != Safari
when('version').gt(3)              // version > 3
when('version').gte(10)            // version >= 10
when('priority').lt(5)             // priority < 5
when('priority').lte(1)            // priority <= 1
```

### IN and LIKE

```typescript
when('env').in('staging', 'prod')  // env IN {staging, prod}
when('browser').like('chrome*')    // browser LIKE chrome*
```

### Parameter-to-Parameter

```typescript
when('start_date').lt('end_date')  // start_date < end_date
```

### IF / THEN / ELSE

```typescript
when('os').eq('Windows')
  .then(when('browser').ne('Safari'))
// IF os = Windows THEN browser != Safari

when('os').eq('mac')
  .then(when('browser').ne('ie'))
  .else(when('arch').ne('arm'))
// IF os = mac THEN browser != ie ELSE arch != arm
```

### Logical Composition

```typescript
// AND
allOf(when('os').eq('win'), when('arch').eq('x64'))
// os = win AND arch = x64

// OR
anyOf(when('os').eq('win'), when('os').eq('linux'))
// os = win OR os = linux

// NOT
not(allOf(when('os').eq('win'), when('browser').eq('safari')))
// NOT (os = win AND browser = safari)
```

### Using with generate()

Builder objects must be converted to strings with `.toString()`:

```typescript
const cw = await Coverwise.create();

cw.generate({
  parameters: [/* ... */],
  constraints: [
    when('os').eq('Windows').then(when('browser').ne('Safari')).toString(),
    when('device').eq('phone').then(when('os').in('iOS', 'Android')).toString(),
  ],
});
```

String constraints and builder constraints can be mixed freely.
