// Golden corpus for decimal parsing, shared by the C++ and TypeScript tests.
//
// Every surface must turn these decimals into the same double: the native CLI
// and the Python wheel (which may compile either parsing backend depending on
// the platform's standard library), WASM, and the pure-TypeScript port. The
// expected value is written as the raw IEEE-754 bit pattern of JavaScript's
// Number(text), which is the cross-language reference for numeric parsing; the
// TypeScript test additionally asserts that each pattern really is Number(text),
// so the file cannot drift away from the language it claims to follow.
//
// Each entry is COVERWISE_NUMERIC_CASE(text, bits, literal disposition), where
// the disposition says whether a relational constraint literal spelled that way
// is accepted (kAcceptLiteral) or reported as an out-of-range decimal
// (kRejectLiteral). Overflow and underflow to zero are rejected; everything
// representable, subnormals included, is accepted.
//
// C++ consumers define COVERWISE_NUMERIC_CASE before including this file; the
// TypeScript consumers read it with the regular expression in
// src/ts/util/numeric-parse-corpus.ts.

// Smallest positive subnormal, and the smallest normal just above the
// subnormal range.
COVERWISE_NUMERIC_CASE("5e-324", 0x0000000000000001ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("4.9e-324", 0x0000000000000001ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("2.2250738585072014e-308", 0x0010000000000000ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("1.1125369292536007e-308", 0x0008000000000000ull, kAcceptLiteral)

// Subnormals in the middle of the range, where a backend that reports
// underflow but keeps its rounded result and one that reports no error at all
// have to agree.
COVERWISE_NUMERIC_CASE("1e-310", 0x000012688b70e62bull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("-1e-310", 0x800012688b70e62bull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("1e-311", 0x000001d74124e3d1ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("1e-320", 0x00000000000007e8ull, kAcceptLiteral)

// Out of range in both directions: the decimal collapses onto a signed zero or
// an infinity, and a constraint literal spelled that way is rejected.
COVERWISE_NUMERIC_CASE("1e-400", 0x0000000000000000ull, kRejectLiteral)
COVERWISE_NUMERIC_CASE("-1e-400", 0x8000000000000000ull, kRejectLiteral)
COVERWISE_NUMERIC_CASE("1e400", 0x7ff0000000000000ull, kRejectLiteral)
COVERWISE_NUMERIC_CASE("-1e400", 0xfff0000000000000ull, kRejectLiteral)
COVERWISE_NUMERIC_CASE("1.7976931348623157e308", 0x7fefffffffffffffull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("1.7976931348623159e308", 0x7ff0000000000000ull, kRejectLiteral)

// Ordinary decimals and the signed zeros, which must keep parsing as they did.
COVERWISE_NUMERIC_CASE("0", 0x0000000000000000ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("-0", 0x8000000000000000ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("0.0e999", 0x0000000000000000ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("0.1", 0x3fb999999999999aull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("-3.0E-2", 0xbf9eb851eb851eb8ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE("12.", 0x4028000000000000ull, kAcceptLiteral)
COVERWISE_NUMERIC_CASE(".5", 0x3fe0000000000000ull, kAcceptLiteral)
