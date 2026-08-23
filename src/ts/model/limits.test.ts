import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import * as limits from './limits.js';

/**
 * The C++ and TypeScript limit lists are two files, so nothing at build time
 * makes one follow the other. This test is that mechanism: it parses the header
 * and compares every constant, mapping kFooBar <-> FOO_BAR, and fails if either
 * side gains, loses, or changes an entry.
 */

const headerPath = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../model/limits.h',
);

/** Convert a C++ `kMaxStringBytes` identifier to its TypeScript spelling. */
function toScreamingSnake(name: string): string {
  return name
    .replace(/^k/, '')
    .replace(/([a-z0-9])([A-Z])/g, '$1_$2')
    .toUpperCase();
}

/** Evaluate the restricted arithmetic the header uses (`64 * 1024`, `100000`). */
function evaluateLiteral(expression: string): number {
  return expression
    .split('*')
    .map((factor) => Number(factor.trim()))
    .reduce((product, factor) => product * factor, 1);
}

function parseHeaderConstants(): Map<string, number> {
  const source = readFileSync(headerPath, 'utf8');
  const pattern = /inline constexpr size_t (k\w+) = ([^;]+);/g;
  const found = new Map<string, number>();
  for (const match of source.matchAll(pattern)) {
    found.set(toScreamingSnake(match[1]), evaluateLiteral(match[2]));
  }
  return found;
}

describe('input limits', () => {
  const headerConstants = parseHeaderConstants();

  it('parses every constant out of the C++ header', () => {
    expect(headerConstants.size).toBeGreaterThan(0);
    for (const value of headerConstants.values()) {
      expect(Number.isSafeInteger(value)).toBe(true);
    }
  });

  it('declares the same constant names on both sides', () => {
    expect([...headerConstants.keys()].sort()).toEqual(Object.keys(limits).sort());
  });

  it('agrees on every value', () => {
    for (const [name, value] of headerConstants) {
      expect({ [name]: (limits as Record<string, number>)[name] }).toEqual({ [name]: value });
    }
  });
});
