/// UTF-8 byte accounting shared by the acceptance rules.

/**
 * Number of UTF-8 bytes a string encodes to.
 *
 * Matches `new TextEncoder().encode(value).byteLength` and the `size()` of the
 * equivalent C++ std::string, without allocating a buffer per call — the
 * acceptance rules walk every string in a model, so the allocation dominates.
 * Unpaired surrogates encode as the replacement character (3 bytes), which is
 * what TextEncoder does.
 *
 * @param value - The string to measure.
 * @returns The UTF-8 byte length.
 */
export function utf8ByteLength(value: string): number {
  let bytes = 0;
  for (let i = 0; i < value.length; ++i) {
    const code = value.charCodeAt(i);
    if (code < 0x80) {
      bytes += 1;
    } else if (code < 0x800) {
      bytes += 2;
    } else if (code >= 0xd800 && code <= 0xdbff && i + 1 < value.length) {
      const low = value.charCodeAt(i + 1);
      if (low >= 0xdc00 && low <= 0xdfff) {
        bytes += 4;
        ++i;
      } else {
        bytes += 3;
      }
    } else {
      bytes += 3;
    }
  }
  return bytes;
}
