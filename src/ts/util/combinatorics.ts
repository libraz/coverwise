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
    remainder = (remainder / radixes[i]) | 0;
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
