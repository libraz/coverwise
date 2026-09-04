# Input limits

Seven limits bound what a model may declare, and every surface applies the same seven: the command line, the Python package, the WebAssembly build, the pure-TypeScript port and the C++ library. They are part of what coverwise accepts, not of how it is tuned — the internal budgets that bound how much work a single decision may cost are not documented here and are not stable. Exceeding a limit is a rejection with a message naming the limit, never a truncated or partial result.

## The declared limits

| Limit | Value |
|-------|-------|
| Parameters in one model | 1,024 |
| Values in one parameter | 16,384 |
| Rows in a `tests`, `seeds` or `existing` array | 100,000 |
| Constraint expressions in one model | 256 |
| UTF-8 bytes in one input string | 65,536 (64 KiB) |
| UTF-8 bytes across one input's strings, combined | 1,048,576 (1 MiB) |
| Bytes of one JSON document a surface reads | 67,108,864 (64 MiB) |

The parameter count is what keeps constraint feasibility search bounded. The search walks one parameter per level, and a satisfying chain spends one node of the search budget per level, so nothing else limits how deep a search can go.

The last row is different in kind from the six above it; the section on the document guard, below, says why.

## What a caller sees

A limit is checked before generation starts, so a rejection costs nothing and names the limit that fired.

| Surface | How a rejection arrives |
|---|---|
| Command line | Exit code 3, with the message on standard error |
| Python | `CoverwiseError` raised, carrying the message |
| JavaScript and TypeScript | `CoverwiseError` thrown, carrying the message |
| C++ | The returned result's `error` is non-ok with `code` set to `kInvalidInput` |

The messages name the limit and the value that exceeded it. From the command line they read like this:

- `parameters exceed maximum of 1024`
- `parameter 'p0' values exceed maximum of 16384`
- `seeds exceed maximum of 100000`
- `constraints exceed maximum of 256`
- `p0[0] exceeds 65536 UTF-8 bytes` — the offending string is named by its position
- `Input strings exceed 1048576 UTF-8 bytes`

## What counts against the string budgets

Two of the seven are byte budgets rather than counts. The per-string limit applies to each input string on its own. The aggregate limit is one budget for the whole input, and every string is charged against it once, where it is read: parameter names, values, aliases, equivalence-class names, constraint expressions, sub-model parameter names, the parameter and value names a `weights` object is keyed by, and the values of every row in a `tests`, `seeds` or `existing` array.

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ],
  "constraints": ["IF os = Windows THEN browser != Safari"]
}
```

That model spends 83 bytes of the aggregate budget: 19 for the first parameter's name and values, 26 for the second's, and 38 for the constraint expression. Nothing else in the document is charged — braces, quotes, colons and the `name`/`values`/`constraints` keys are JSON syntax, not text the caller supplied.

A row is charged for its **values**, never for its keys. A key is a parameter name, already charged once as a model string, and charging it again per row would count the same text once per row. Only **string** values are charged; a numeric or boolean row value costs nothing.

Building a suite in C++ is the one case where rows cost nothing at all, because a C++ row carries value indices rather than value names and an index is not text. The same suite written as JSON is charged for every string value in every row, so a large suite can be accepted by the library and refused on the command line. See the [C++ API](cpp-api.md) for the full statement of that difference.

## Which limit binds first

The aggregate byte budget binds before the row ceiling for most models. 100,000 rows is a ceiling, not a promise that a suite of that size is accepted: 1 MiB spread over 100,000 rows leaves about 10.5 bytes of row text per row, which only a very narrow model fits. With 5-byte string values and one value per parameter per row, the two limits meet like this:

| Parameters per row | Rows accepted |
|--------------------|---------------|
| 2 | 100,000 (the row ceiling binds) |
| 3 | 69,902 |
| 10 | 20,968 |
| 100 | 2,094 |

The ceiling is a function of both dimensions, so these figures assume the model's own strings are small next to the rows; a model with long names or many values spends part of the same budget and lowers them.

This is why a rejection well under 100,000 rows is a real limit rather than a bug: the message names the budget, not the row count. Shorter value names buy rows directly, and a suite the budget will not hold has to be analyzed in slices.

## The document guard

The document limit is not part of the acceptance contract. It is a memory guard applied to the raw bytes of a file or a standard-input stream as they arrive, before any of them are parsed, so that a runaway or truncated stream cannot be read into memory without end.

It therefore counts JSON syntax — every brace, quote, colon and repeated key — while the aggregate byte budget counts only the text a caller supplied. For models of ordinary width it sits far outside the other six and a caller meets one of those first. For a wide model the syntax dominates and the guard is what a document meets first: 100 parameters at 100,002 rows is about 133 MiB of JSON carrying far less than 1 MiB of row text, and it is refused with `file '<path>' exceeds the maximum of 67108864 bytes`. Read that message as "this file is too large to read", not as a statement about the suite, which may be well inside every limit above.

The guard applies only to surfaces that parse a document. The command line is one. The TypeScript surfaces are not: they take an already-parsed object, so there is no document for them to bound, and the constant exists on that side only so the two limit lists stay comparable. The Python package builds a document and hands it to the command-line binary, so the guard applies to what it sends.

## Where to go next

- [CLI reference](cli.md) — the commands these limits apply to, and the exit-code contract
- [JavaScript API](js-api.md) — the four published entry points and the errors they throw
- [Python API](python-api.md) — the Python surface and its exception type
- [C++ API](cpp-api.md) — the constants that declare these limits, for embedders
- [Performance](performance.md) — what generation costs well inside these limits
