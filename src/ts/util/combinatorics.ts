/// @file combinatorics.ts
/// @brief Combinatorial utility functions.

/// Generate all C(n, k) combinations of indices [0, n).
///
/// @param n The total number of elements.
/// @param k The size of each combination.
/// @returns Array of sorted index arrays. Empty if k === 0 or k > n.
export function generateCombinations(n: number, k: number): number[][] {
  const result: number[][] = [];
  if (k === 0 || k > n) {
    return result;
  }

  const indices = new Array<number>(k);
  for (let i = 0; i < k; i++) {
    indices[i] = i;
  }

  while (true) {
    result.push(indices.slice());

    // Find rightmost index that can be incremented.
    let pos = k - 1;
    while (pos >= 0 && indices[pos] === n - k + pos) {
      pos--;
    }
    if (pos < 0) {
      break;
    }

    indices[pos]++;
    for (let j = pos + 1; j < k; j++) {
      indices[j] = indices[j - 1] + 1;
    }
  }

  return result;
}

/** Generate C(n, k) combinations in one contiguous flat buffer. */
export function generateCombinationsFlat(n: number, k: number): number[] {
  if (k === 0 || k > n) {
    return [];
  }
  const result: number[] = [];
  const indices = new Array<number>(k);
  for (let i = 0; i < k; i++) {
    indices[i] = i;
  }
  while (true) {
    for (let i = 0; i < k; i++) {
      result.push(indices[i]);
    }
    let pos = k - 1;
    while (pos >= 0 && indices[pos] === n - k + pos) {
      pos--;
    }
    if (pos < 0) {
      return result;
    }
    indices[pos]++;
    for (let j = pos + 1; j < k; j++) {
      indices[j] = indices[j - 1] + 1;
    }
  }
}

/** Compute C(n, k), returning null when it exceeds limit or safe-integer range. */
export function checkedBinomial(n: number, k: number, limit: number): number | null {
  if (k > n) {
    return 0;
  }
  const reducedK = Math.min(k, n - k);
  let result = 1;
  for (let i = 1; i <= reducedK; ++i) {
    result = (result * (n - reducedK + i)) / i;
    if (!Number.isSafeInteger(result) || result > limit) {
      return null;
    }
  }
  return result;
}

/// Decode a flat (mixed-radix) index into per-position value indices.
///
/// @param flatIndex The flat index to decode.
/// @param radixes The radix (number of values) for each position, in order.
/// @returns Array of decoded indices (same length as radixes).
export function decodeMixedRadix(flatIndex: number, radixes: readonly number[]): number[] {
  const out = new Array<number>(radixes.length);
  let remainder = flatIndex;
  for (let i = radixes.length - 1; i >= 0; i--) {
    out[i] = remainder % radixes[i];
    // Math.floor, not `| 0`: bitwise OR coerces to a signed 32-bit int and would
    // corrupt indices at or above 2^31.
    remainder = Math.floor(remainder / radixes[i]);
  }
  return out;
}

/// Encode per-position value indices into a flat (mixed-radix) index.
///
/// @param indices The per-position value indices.
/// @param radixes The radix (number of values) for each position, in order.
/// @returns The flat index.
export function encodeMixedRadix(indices: readonly number[], radixes: readonly number[]): number {
  let result = 0;
  for (let i = 0; i < indices.length; i++) {
    result = result * radixes[i] + indices[i];
  }
  return result;
}
