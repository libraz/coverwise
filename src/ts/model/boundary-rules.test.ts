import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import {
  BOUNDARY_METADATA_FIELDS,
  boundaryAcceptanceError,
  INTEGER_BOUNDARY_STEP,
} from './boundary-rules.js';

/**
 * The boundary rules are stated twice — once here, once in C++ — so nothing at
 * build time makes one follow the other. This test is that mechanism: it reads
 * the string literals out of the C++ model layer and requires every refusal
 * this module can produce to be spelled there character for character. Wording
 * changed on one side alone, or a threshold that renders into the text (the
 * integer step) changed on one side alone, fails here.
 */

/** The C++ model-layer files that refuse a boundary parameter. */
const SOURCE_PATHS = ['../../model/options_validation.cpp', '../../model/boundary.cpp'].map(
  (relative) => path.resolve(path.dirname(fileURLToPath(import.meta.url)), relative),
);

/**
 * Stand-ins for the parameter name and value a message carries. Splitting a
 * rendered message on them leaves exactly the text the C++ side writes out as
 * string literals around its own concatenations.
 */
const PLACEHOLDERS = ['<parameter-name>', '<parameter-value>'] as const;

/** Every string literal in the C++ sources, as the compiler would see them. */
function parseStringLiterals(): string[] {
  const literals = new Set<string>();
  for (const sourcePath of SOURCE_PATHS) {
    const source = readFileSync(sourcePath, 'utf8');
    for (const match of source.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)) {
      literals.add(match[1]);
    }
  }
  return [...literals];
}

/** The fixed text of a message, split at the values it interpolates. */
function literalFragments(build: (...args: string[]) => string): string[] {
  return build(...PLACEHOLDERS)
    .split(new RegExp(PLACEHOLDERS.join('|')))
    .filter((fragment) => fragment.length > 0);
}

describe('boundary acceptance rules', () => {
  const cppLiterals = parseStringLiterals();

  it('reads the C++ model layer', () => {
    expect(cppLiterals.length).toBeGreaterThan(0);
  });

  it('renders the integer step threshold into the message that states it', () => {
    expect(boundaryAcceptanceError.integerStep('n')).toContain(String(INTEGER_BOUNDARY_STEP));
  });

  it('names the metadata fields the same way the C++ model layer does', () => {
    for (const field of BOUNDARY_METADATA_FIELDS) {
      expect(cppLiterals, field).toContain(field);
    }
  });

  for (const [rule, build] of Object.entries(boundaryAcceptanceError)) {
    it(`states the ${rule} refusal in the same words as the C++ model layer`, () => {
      for (const fragment of literalFragments(build as (...args: string[]) => string)) {
        expect(cppLiterals, `${rule}: ${JSON.stringify(fragment)}`).toContain(fragment);
      }
    });
  }
});
