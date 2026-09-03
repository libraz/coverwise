import { describe, expect, it } from 'vitest';
import { utf8ByteLength } from './utf8.js';

/**
 * The measurement the acceptance budgets are stated in.
 *
 * `utf8ByteLength` counts what `TextEncoder` would produce without building the
 * buffer, so the two must agree on every input or the documented byte limits
 * mean one thing in the model layer and another wherever a caller measures.
 * These compare the two directly rather than restating expected counts: the
 * platform encoder is the specification here, including its rule that a lone
 * surrogate encodes as the replacement character.
 */

const encoder = new TextEncoder();

function encoded(value: string): number {
  return encoder.encode(value).byteLength;
}

/** A repeatable stream, so a disagreement can be reproduced from the seed. */
function pseudoRandom(seed: number): () => number {
  let state = seed >>> 0;
  return () => {
    state = (state * 1664525 + 1013904223) >>> 0;
    return state / 0x100000000;
  };
}

describe('utf8ByteLength', () => {
  const cases: Array<{ label: string; value: string }> = [
    { label: 'the empty string', value: '' },
    { label: 'ASCII', value: 'parameter_name-1.2' },
    { label: 'the last one-byte code point', value: '\u007f' },
    { label: 'the first two-byte code point', value: '\u0080' },
    { label: 'the last two-byte code point', value: '߿' },
    { label: 'the first three-byte code point', value: 'ࠀ' },
    { label: 'the last three-byte code point', value: '￿' },
    { label: 'the first four-byte code point', value: '\u{10000}' },
    { label: 'the last code point', value: '\u{10ffff}' },
    { label: 'Japanese', value: '日本語のパラメータ' },
    { label: 'a combining mark', value: 'é' },
    { label: 'an emoji', value: '🎯' },
    { label: 'a ZWJ sequence', value: '👨‍👩‍👧' },
    { label: 'a lone high surrogate', value: '\ud800' },
    { label: 'a lone low surrogate', value: '\udc00' },
    { label: 'a high surrogate at the end', value: 'a\ud800' },
    { label: 'a high surrogate followed by a letter', value: '\ud800a' },
    { label: 'a low surrogate before a high one', value: '\udc00\ud800' },
    { label: 'a valid surrogate pair', value: '😀' },
    { label: 'mixed text', value: 'os=win / 環境=本番 / 🎯 / \ud800' },
  ];

  for (const { label, value } of cases) {
    it(`measures ${label} as TextEncoder does`, () => {
      expect(utf8ByteLength(value)).toBe(encoded(value));
    });
  }

  it('measures randomly built strings as TextEncoder does', () => {
    const next = pseudoRandom(0x5eed);
    for (let iteration = 0; iteration < 500; ++iteration) {
      const length = Math.floor(next() * 24);
      let value = '';
      for (let i = 0; i < length; ++i) {
        // Draws from the whole BMP, so lone surrogates appear on their own and
        // in pairs without being arranged for.
        value += String.fromCharCode(Math.floor(next() * 0x10000));
      }
      expect({ value, bytes: utf8ByteLength(value) }).toEqual({ value, bytes: encoded(value) });
    }
  });

  it('measures a long string as TextEncoder does', () => {
    const value = '日本語x🎯'.repeat(4096);
    expect(utf8ByteLength(value)).toBe(encoded(value));
  });
});
