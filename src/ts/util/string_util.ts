/// @file string_util.ts
/// @brief General-purpose string conversion utilities.

/// Check if a string can be parsed as a finite number.
///
/// Matches the behavior of the C++ version: the entire string must be a valid
/// numeric representation (no leading/trailing whitespace beyond what Number()
/// accepts, no empty strings, no non-finite values).
export function isNumeric(s: string): boolean {
  if (s.length === 0) {
    return false;
  }
  // Reject strings that are only whitespace or contain non-numeric content.
  if (s.trim().length === 0) {
    return false;
  }
  const n = Number(s);
  return Number.isFinite(n);
}

/// Parse a string as a number (double-precision float).
///
/// Assumes the caller has verified the string is numeric via isNumeric().
export function toDouble(s: string): number {
  return Number(s);
}
