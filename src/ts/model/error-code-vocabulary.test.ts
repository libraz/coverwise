import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { type CoverwiseErrorCode, errorCodeFromNumber } from '../../../js/types.js';
import { ErrorCode } from './error.js';

/**
 * The error-code vocabulary is declared three times — the C++ enum, the
 * TypeScript enum that mirrors it, and the string union the npm surface throws
 * — and each declaration spells the same categories differently. Nothing in
 * either language makes one follow another, so this is the mechanism that does:
 * it parses the enum out of the C++ header and fails if any of the three gains,
 * loses, renames or renumbers a category.
 *
 * The numeric codes cross the WASM boundary, so a renumbering is not a cosmetic
 * change: it silently relabels every error a caller catches.
 *
 * The exit-code table the Python package documents is deliberately not checked
 * here. It mirrors the command-line exit codes rather than this enum, and those
 * fold two categories onto one number by design.
 */

const headerPath = path.resolve(
  path.dirname(fileURLToPath(import.meta.url)),
  '../../model/error.h',
);

/** Convert a C++ `kConstraintError` enumerator to its TypeScript spelling. */
function toPascal(enumerator: string): string {
  return enumerator.replace(/^k/, '');
}

/** Convert a C++ `kConstraintError` enumerator to its npm string code. */
function toScreamingSnake(enumerator: string): string {
  return toPascal(enumerator)
    .replace(/([a-z0-9])([A-Z])/g, '$1_$2')
    .toUpperCase();
}

/** The `Error::Code` enumerators, in declaration order, with their values. */
function parseHeaderCodes(): Map<string, number> {
  const source = readFileSync(headerPath, 'utf8');
  const body = source.match(/enum class Code \{([^}]*)\}/);
  const found = new Map<string, number>();
  if (body === null) {
    return found;
  }
  for (const entry of body[1].matchAll(/(k\w+)\s*=\s*(\d+)/g)) {
    found.set(entry[1], Number(entry[2]));
  }
  return found;
}

/** The members of the npm string union, read from its declaration. */
function parseUnionMembers(): string[] {
  const typesPath = path.resolve(
    path.dirname(fileURLToPath(import.meta.url)),
    '../../../js/types.ts',
  );
  const source = readFileSync(typesPath, 'utf8');
  const declaration = source.match(/export type CoverwiseErrorCode =([^;]*);/);
  if (declaration === null) {
    return [];
  }
  return [...declaration[1].matchAll(/'([A-Z_]+)'/g)].map((member) => member[1]);
}

describe('the error code vocabulary', () => {
  const headerCodes = parseHeaderCodes();
  const failures = [...headerCodes].filter(([name]) => name !== 'kOk');

  it('parses the enumeration out of the C++ header', () => {
    expect(headerCodes.size).toBeGreaterThan(1);
    expect(headerCodes.get('kOk')).toBe(0);
    for (const value of headerCodes.values()) {
      expect(Number.isSafeInteger(value)).toBe(true);
    }
  });

  it('is declared with the same names and numbers in TypeScript', () => {
    const declared = Object.keys(ErrorCode).filter((key) => !/^\d+$/.test(key));
    expect(declared.sort()).toEqual([...headerCodes.keys()].map(toPascal).sort());
    for (const [name, value] of headerCodes) {
      const member = toPascal(name);
      expect({ [member]: (ErrorCode as Record<string, number>)[member] }).toEqual({
        [member]: value,
      });
    }
  });

  it('offers the same categories as the npm string union', () => {
    // Success is not a category a caller catches, so the union carries the
    // failures alone.
    expect(parseUnionMembers().sort()).toEqual(
      failures.map(([name]) => toScreamingSnake(name)).sort(),
    );
  });

  it('maps every numeric code to the string code of the same category', () => {
    for (const [name, value] of failures) {
      const expected = toScreamingSnake(name) as CoverwiseErrorCode;
      expect({ [value]: errorCodeFromNumber(value) }).toEqual({ [value]: expected });
    }
  });

  it('falls back to invalid input for a number the enumeration does not declare', () => {
    const beyond = Math.max(...headerCodes.values()) + 1;
    expect(errorCodeFromNumber(beyond)).toBe('INVALID_INPUT');
    expect(errorCodeFromNumber(undefined)).toBe('INVALID_INPUT');
  });
});
