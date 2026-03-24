# Introduction

## What Is coverwise?

coverwise is a combinatorial test generation engine. Given a set of parameters and their values, it produces a minimal set of test cases that covers every t-wise combination — guaranteeing that every pair (or triple, or higher-order group) of parameter values appears in at least one test.

It runs in **browsers**, **Node.js**, and as **native C++**, with zero platform-specific dependencies in the JavaScript build thanks to WebAssembly.

## The Problem

Consider a web app that supports 3 operating systems, 4 browsers, and 3 screen sizes. That's 3 × 4 × 3 = **36 possible combinations**. Add a few more parameters and you quickly reach thousands or millions.

Testing all combinations is impractical. Testing a random subset leaves gaps. Manually picking "representative" tests is error-prone and hard to maintain.

## The Solution: Pairwise Testing

Research shows that most software bugs are triggered by the interaction of **two parameters** (pairwise), not all parameters simultaneously. Pairwise testing exploits this:

| Approach | Tests for 4 params × 3 values each |
|----------|-------------------------------------|
| Exhaustive | 81 tests |
| Pairwise (2-wise) | ~9 tests |
| 3-wise | ~27 tests |

coverwise generates these minimal covering arrays automatically, with mathematical proof that every required combination is present.

## Key Concepts

### Parameters and Values

A **parameter** is a dimension of variation in your system under test. Each parameter has a finite set of **values**.

```json
{
  "parameters": [
    { "name": "os", "values": ["Windows", "macOS", "Linux"] },
    { "name": "browser", "values": ["Chrome", "Firefox", "Safari"] }
  ]
}
```

### Strength

**Strength** (t) defines how many parameters are considered simultaneously:

- **t = 2** (pairwise): Every pair of parameter values appears together in at least one test
- **t = 3** (3-wise): Every triple of parameter values appears in at least one test
- Higher strength = more tests, stronger guarantees

### Constraints

Real systems have **constraints** — not every combination is valid. For example, Safari only runs on macOS. coverwise enforces these during generation:

```
IF os = Windows THEN browser != Safari
```

No invalid combination will ever appear in the output.

### Coverage

**Coverage** measures the fraction of required t-wise tuples that appear in your test suite. coverwise always targets 100% coverage, and reports exactly which combinations are missing if it can't reach that (e.g., due to a `maxTests` limit).

## Design Philosophy

coverwise is built as a **test design API**, not just a generation tool:

- **JSON in, JSON out** — No DSL to learn for basic usage
- **Stateless** — `generate(input) → output`, no session management
- **Decomposable** — Separate functions for generate, analyze, extend
- **Explainable** — Every result includes coverage proof and uncovered tuples in human-readable format
- **Deterministic** — Same input + seed = same output, every time

## Platform Support

| Platform | Method |
|----------|--------|
| Node.js (≥ 18) | `npm install @libraz/coverwise` |
| Browser | ESM import (WASM loaded automatically) |
| C++ (native) | CMake, static library |
| CLI | `coverwise` binary |

## Next Steps

- [Getting Started](getting-started.md) — Install and generate your first test suite
- [Examples](examples.md) — Real-world usage patterns
- [Constraint Syntax](constraints.md) — Write constraint expressions
