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

## Performance

All configurations achieve **100% t-wise coverage**, verified by an independent coverage validator. Test counts fall within known theoretical bounds from covering array research.

### Pairwise (2-wise) Generation

| Configuration | Params | Values | Tuples | Tests | Theoretical Min | Time |
|---------------|--------|--------|--------|-------|-----------------|------|
| 5 × 3 uniform | 5 | 3 | 90 | 16 | 9 (OA) | < 1 ms |
| 10 × 3 uniform | 10 | 3 | 405 | 20 | 9 (OA) | < 1 ms |
| 13 × 3 uniform | 13 | 3 | 702 | 21 | 9 (OA) | < 1 ms |
| 10 × 5 uniform | 10 | 5 | 1,125 | 52 | 25 | 1 ms |
| 15 × 4 uniform | 15 | 4 | 1,680 | 40 | 16 | 1 ms |
| 20 × 2 uniform | 20 | 2 | 760 | 12 | 4 | < 1 ms |
| 20 × 5 uniform | 20 | 5 | 4,750 | 66 | 25 | 4 ms |
| 30 × 5 uniform | 30 | 5 | 10,875 | 76 | 25 | 9 ms |
| 50 × 3 uniform | 50 | 3 | 11,025 | 33 | 9 (OA) | 6 ms |
| 5 × 20 high-card | 5 | 20 | 4,000 | 514 | 400 | 9 ms |
| 3⁴ × 2³ mixed | 7 | 2–3 | 138 | 14 | 9 | < 1 ms |
| 5¹ × 3³ × 2⁴ mixed | 8 | 2–5 | 208 | 19 | 15 | < 1 ms |

### Higher-Strength Generation

| Configuration | Params | Values | Strength | Tuples | Tests | Time |
|---------------|--------|--------|----------|--------|-------|------|
| 15 × 3 | 15 | 3 | 3-wise | 12,285 | 100 | 11 ms |
| 8 × 3 | 8 | 3 | 4-wise | 5,670 | 236 | 8 ms |

Measured on Apple M-series (seed=42). "Theoretical Min" refers to known lower bounds from orthogonal array (OA) theory or v² bounds. Greedy algorithms typically produce 1.5–2.5× the theoretical minimum — this is expected and consistent with published results for covering array generators.

## Next Steps

- [Getting Started](getting-started.md) — Install and generate your first test suite
- [Examples](examples.md) — Real-world usage patterns
- [Constraint Syntax](constraints.md) — Write constraint expressions
