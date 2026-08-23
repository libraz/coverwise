# Security policy

## Reporting a vulnerability

Report privately, not through the public issue tracker:

- **Preferred:** GitHub's private vulnerability reporting, from the repository's
  [Security tab](https://github.com/libraz/coverwise/security/advisories/new).
- **Alternative:** email `libraz@libraz.net`.

Include the input that triggers it, what happened, the affected version, and a
minimal reproduction if you have one. Expect an acknowledgement within a few
days.

## Supported versions

Fixes land on the latest minor of the current major line and on the minor before
it, so a deployment has one minor release of room to upgrade.

| Version | Supported |
|---------|-----------|
| v1.x    | latest minor + previous minor |
| v0.x    | unsupported |

## What is in scope

coverwise reads test files and parameter models it did not write, so the parsing
and generation path is the interesting surface. In scope:

- A crafted test file, parameter model or constraint set that causes a crash, a
  hang, unbounded memory growth, or a read outside the tree being analysed —
  in the C++ core or through the Node and WebAssembly packaging.
- Any path by which analysing a tree executes code from that tree. Nothing being
  analysed is supposed to run.
- Any path by which the analysed source or the generated suite leaves the
  machine. This tool is meant to make no network calls.
- Escapes from the WebAssembly sandbox.

## What is not in scope

- A generated suite that misses a combination. Coverage gaps are correctness
  bugs; report them as normal issues.
- Documented limits behaving as documented — parameter-count and combination
  ceilings exist so a hostile model cannot run the generator out of memory. A
  ceiling that can be bypassed is in scope.
- Vulnerabilities in the code under test. coverwise reports coverage; it is not
  a vulnerability scanner.
- Findings that require an attacker to already control the machine the analysis
  runs on.
