import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { type CoverwiseErrorCode, errorCodeFromNumber } from '../../../js/types.js';
import {
  discoverSources,
  isTestPath,
  type LexicalFamily,
  lexSource,
} from '../../../tests/util/source-scan.js';
import { ErrorCode } from './error.js';

/**
 * The error-code vocabulary is declared three times — the C++ enum, the
 * TypeScript enum that mirrors it, and the string union the npm surface throws
 * — and each declaration spells the same categories differently. Nothing in
 * either language makes one follow another, so this is the mechanism that does:
 * it parses the enum and the union out of the sources and fails if any of the
 * three gains, loses, renames or renumbers a category.
 *
 * The numeric codes cross the WASM boundary, so a renumbering is not a cosmetic
 * change: it silently relabels every error a caller catches.
 *
 * Both declarations are found by their own syntax across the sources rather
 * than read out of files named here, so moving either one is not something the
 * check has to be told about, and losing either one is a failure rather than an
 * empty comparison that agrees with itself.
 *
 * The exit-code table the Python package documents is deliberately not checked
 * here. It mirrors the command-line exit codes rather than this enum, and those
 * fold two categories onto one number by design.
 */

/** Sources of one syntax family that the engine ships, found by where they are. */
function sourcesOf(family: LexicalFamily): string[] {
  return discoverSources()
    .programs.filter((source) => source.family === family && !isTestPath(source.relativePath))
    .map(
      (source) =>
        lexSource(readFileSync(source.absolutePath, 'utf8'), source.family).withoutComments,
    );
}

/** The one match of @p pattern across @p texts, or a refusal to guess. */
function theOne(texts: readonly string[], pattern: string, what: string): string {
  const found: string[] = [];
  for (const text of texts) {
    for (const match of text.matchAll(new RegExp(pattern, 'g'))) {
      found.push(match[1]);
    }
  }
  if (found.length !== 1) {
    throw new Error(`Expected exactly one ${what} in the sources, found ${found.length}.`);
  }
  return found[0];
}

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

/**
 * The `Error::Code` enumerators, in declaration order, with their values.
 *
 * Every enumerator the body lists has to be read. One written in a shape this
 * does not recognise -- a value left implicit, a name that is not `kSomething`
 * -- would otherwise be dropped, and the comparisons below would report full
 * agreement over a category none of them looked at.
 */
function parseEnumerators(body: string): Map<string, number> {
  const found = new Map<string, number>();
  for (const entry of body.matchAll(/(k\w+)\s*=\s*(\d+)/g)) {
    found.set(entry[1], Number(entry[2]));
  }
  const listed = body
    .split(',')
    .map((entry) => entry.trim())
    .filter((entry) => entry.length > 0).length;
  if (listed !== found.size) {
    throw new Error(
      `The enumeration lists ${listed} enumerators but ${found.size} were read; one is written in a shape this cannot parse.`,
    );
  }
  return found;
}

/** The members of a string union, read from the body of its declaration. */
function parseUnionMembers(body: string): string[] {
  return [...body.matchAll(/'([A-Z_]+)'/g)].map((member) => member[1]);
}

/** The three declarations, as one thing to compare. */
interface Vocabulary {
  /** The C++ enumerators and their values. */
  cpp: Map<string, number>;
  /** The TypeScript enum that mirrors it, by its own spelling. */
  typescript: Record<string, number>;
  /** The npm string union's members. */
  union: string[];
}

/**
 * Everywhere the three declarations disagree, named one by one.
 *
 * Reported rather than thrown at the first difference: a renumbering that also
 * renamed a category should say both things, and a comparison that stopped at
 * the first would send the reader back around for the second.
 */
function disagreements(vocabulary: Vocabulary): string[] {
  const found: string[] = [];
  const declaredInTypeScript = Object.keys(vocabulary.typescript).filter(
    (key) => !/^\d+$/.test(key),
  );
  const expectedInTypeScript = [...vocabulary.cpp.keys()].map(toPascal);
  for (const name of expectedInTypeScript) {
    if (!declaredInTypeScript.includes(name)) {
      found.push(`TypeScript does not declare ${name}`);
    }
  }
  for (const name of declaredInTypeScript) {
    if (!expectedInTypeScript.includes(name)) {
      found.push(`TypeScript declares ${name}, which C++ does not`);
    }
  }
  for (const [enumerator, value] of vocabulary.cpp) {
    const mirrored = vocabulary.typescript[toPascal(enumerator)];
    if (mirrored !== undefined && mirrored !== value) {
      found.push(`${toPascal(enumerator)} is ${value} in C++ and ${mirrored} in TypeScript`);
    }
  }

  // Success is not a category a caller catches, so the union carries the
  // failures alone.
  const failures = [...vocabulary.cpp.keys()]
    .filter((name) => name !== 'kOk')
    .map(toScreamingSnake);
  for (const code of failures) {
    if (!vocabulary.union.includes(code)) {
      found.push(`The npm string union does not offer ${code}`);
    }
  }
  for (const code of vocabulary.union) {
    if (!failures.includes(code)) {
      found.push(`The npm string union offers ${code}, which C++ does not`);
    }
  }
  return found.sort();
}

describe('the error code vocabulary', () => {
  const cppTexts = sourcesOf('c-like');
  const tsTexts = sourcesOf('js-like');
  const headerCodes = parseEnumerators(
    theOne(cppTexts, 'enum class Code \\{([^}]*)\\}', 'C++ error enumeration'),
  );
  const union = parseUnionMembers(
    theOne(tsTexts, 'export type CoverwiseErrorCode =([^;]*);', 'npm error-code union'),
  );
  const vocabulary: Vocabulary = {
    cpp: headerCodes,
    typescript: ErrorCode as unknown as Record<string, number>,
    union,
  };

  it('finds all three declarations', () => {
    // Guards the extraction: a declaration that moved out of reach would leave
    // the comparisons below agreeing about nothing.
    expect(headerCodes.size).toBeGreaterThan(1);
    expect(headerCodes.get('kOk')).toBe(0);
    expect(union.length).toBe(headerCodes.size - 1);
    for (const value of headerCodes.values()) {
      expect(Number.isSafeInteger(value)).toBe(true);
    }
  });

  it('names and numbers every category the same way in all three', () => {
    expect(disagreements(vocabulary)).toEqual([]);
  });

  it('maps every numeric code to the string code of the same category', () => {
    for (const [name, value] of headerCodes) {
      if (name === 'kOk') {
        continue;
      }
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

describe('the comparison reports a vocabulary that has drifted', () => {
  const agreeing = (): Vocabulary => ({
    cpp: new Map([
      ['kOk', 0],
      ['kConstraintError', 1],
      ['kTupleExplosion', 2],
    ]),
    typescript: { Ok: 0, ConstraintError: 1, TupleExplosion: 2 },
    union: ['CONSTRAINT_ERROR', 'TUPLE_EXPLOSION'],
  });

  it('accepts three declarations that agree', () => {
    expect(disagreements(agreeing())).toEqual([]);
  });

  it('reports a category renumbered in C++ alone', () => {
    // The number is what crosses the WASM boundary, so a renumbering relabels
    // every error a caller catches while every name still matches.
    const drifted = agreeing();
    drifted.cpp.set('kTupleExplosion', 7);
    expect(disagreements(drifted)).toEqual(['TupleExplosion is 7 in C++ and 2 in TypeScript']);
  });

  it('reports a category the TypeScript enum has not been told about', () => {
    const drifted = agreeing();
    drifted.cpp.set('kInsufficientCoverage', 3);
    expect(disagreements(drifted)).toEqual([
      'The npm string union does not offer INSUFFICIENT_COVERAGE',
      'TypeScript does not declare InsufficientCoverage',
    ]);
  });

  it('reports a category the npm union offers on its own', () => {
    const drifted = agreeing();
    drifted.union.push('INVALID_INPUT');
    expect(disagreements(drifted)).toEqual([
      'The npm string union offers INVALID_INPUT, which C++ does not',
    ]);
  });

  it('reports a renamed enumerator from both sides at once', () => {
    const drifted = agreeing();
    drifted.cpp.delete('kConstraintError');
    drifted.cpp.set('kConstraintFailure', 1);
    expect(disagreements(drifted)).toEqual([
      'The npm string union does not offer CONSTRAINT_FAILURE',
      'The npm string union offers CONSTRAINT_ERROR, which C++ does not',
      'TypeScript declares ConstraintError, which C++ does not',
      'TypeScript does not declare ConstraintFailure',
    ]);
  });

  it('refuses an enumeration it could only partly read', () => {
    // An enumerator whose value is left implicit is one the parse would drop,
    // and a dropped category is one every comparison above would report
    // agreement about.
    expect(() => parseEnumerators('kOk = 0, kConstraintError = 1, kTupleExplosion')).toThrow(
      /shape this cannot parse/,
    );
  });

  it('refuses to guess when a declaration is gone', () => {
    expect(() => theOne([], 'enum class Code \\{([^}]*)\\}', 'C++ error enumeration')).toThrow(
      /found 0/,
    );
  });
});
