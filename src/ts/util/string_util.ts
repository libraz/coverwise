/// @file string_util.ts
/// @brief General-purpose string conversion utilities.

/// Strict decimal numeric grammar shared with the C++ core.
///
/// Accepts an optional leading sign, decimal digits with an optional single
/// '.', and an optional `[eE][+-]?digits` exponent (e.g. "123", "-12.5",
/// "+.5", "12.", "1e9", "-3.0E-2). Rejects "inf"/"nan" (any case), hex,
/// leading/trailing whitespace, the empty string, thousands separators, and
/// multiple dots. Acceptance is identical to C++ `IsNumeric`.
const NUMERIC_RE = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/;

/// Check if a string is a valid decimal number under the strict grammar.
export function isNumeric(s: string): boolean {
  if (s.length === 0) {
    return false;
  }
  return NUMERIC_RE.test(s);
}

/// Parse a string as a number (double-precision float).
///
/// Assumes the caller has verified the string is numeric via isNumeric().
export function toDouble(s: string): number {
  return Number(s);
}

/// Format a number exactly like JavaScript's Number.prototype.toString().
///
/// This is the canonical reference for cross-surface number formatting: the
/// C++ `JsNumberToString` reproduces this algorithm so a numeric value renders
/// to a byte-identical string on every surface (WASM, pure-JS, CLI, boundary
/// expansion). `String(value)` already implements the ECMAScript
/// Number-to-String algorithm, including String(-0) === '0'.
export function jsNumberToString(value: number): string {
  return String(value);
}

/// Fold an ASCII letter (A-Z) to uppercase, leaving every other byte
/// untouched. Code points >= 0x80 are never modified.
function asciiUpperChar(code: number): number {
  if (code >= 0x61 && code <= 0x7a) {
    return code - 0x20;
  }
  return code;
}

/// Uppercase a string using ASCII-only folding.
///
/// Only the letters a-z are folded; all other characters (including non-ASCII
/// code points) are preserved verbatim. This mirrors the C++ core, where
/// case-insensitive matching is ASCII-only by design.
export function asciiToUpper(s: string): string {
  let result = '';
  for (let i = 0; i < s.length; ++i) {
    result += String.fromCharCode(asciiUpperChar(s.charCodeAt(i)));
  }
  return result;
}

/// Order two strings the way the C++ core orders them.
///
/// The core keeps caller-supplied maps in `std::map<std::string>`, which orders
/// keys by UTF-8 bytes, and UTF-8 byte order is code point order — so comparing
/// code points reproduces it. JavaScript's own `<` compares UTF-16 code units
/// instead, which sorts a supplementary character ahead of U+E000..U+FFFF and
/// would make a diagnostic name a different key than the native surface does.
///
/// @returns Negative if `a` sorts first, positive if `b` does, 0 if equal.
export function compareUtf8(a: string, b: string): number {
  const left = Array.from(a);
  const right = Array.from(b);
  const shared = Math.min(left.length, right.length);
  for (let i = 0; i < shared; ++i) {
    // Array.from splits on code points, so each element has exactly one.
    const diff = (left[i].codePointAt(0) as number) - (right[i].codePointAt(0) as number);
    if (diff !== 0) {
      return diff;
    }
  }
  return left.length - right.length;
}

/// Compare two strings for equality using ASCII-only case folding.
///
/// Matches the C++ `CaseInsensitiveEqual`: bytes >= 0x80 are compared
/// exactly, so non-ASCII characters that differ only by Unicode case are
/// treated as unequal.
export function asciiCaseInsensitiveEqual(a: string, b: string): boolean {
  if (a.length !== b.length) {
    return false;
  }
  for (let i = 0; i < a.length; ++i) {
    if (asciiUpperChar(a.charCodeAt(i)) !== asciiUpperChar(b.charCodeAt(i))) {
      return false;
    }
  }
  return true;
}
