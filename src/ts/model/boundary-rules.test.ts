import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import {
  discoverSources,
  isTestPath,
  lexSource,
  type ProgramSource,
} from '../../../tests/util/source-scan.js';
import {
  BOUNDARY_METADATA_FIELDS,
  boundaryAcceptanceError,
  INTEGER_BOUNDARY_STEP,
} from './boundary-rules.js';

/**
 * The boundary rules are stated twice -- once here, once in C++ -- so nothing
 * at build time makes one follow the other. This test is that mechanism: it
 * reads the string literals out of the C++ sources and requires every refusal
 * this module can produce to be spelled there character for character. Wording
 * changed on one side alone, or a threshold that renders into the text (the
 * integer step) changed on one side alone, fails here.
 *
 * Which C++ sources are read is not a list. A refusal that moved to a file
 * nobody added to such a list would go on passing, and moving a message is one
 * of the ordinary things a rewrite does; a list would also have to grow with
 * every new file, which is a step it is possible to skip. So the literals come
 * from every C++ source the repository holds outside its tests. That is a
 * wider pool than the two files these messages live in today, which is the
 * point: what is being asserted is that the wording exists in C++, not that it
 * exists in a particular file.
 */

/** Every C++ source the engine ships, found by where it is. */
function cppSources(): ProgramSource[] {
  return discoverSources().programs.filter(
    (source) => source.family === 'c-like' && !isTestPath(source.relativePath),
  );
}

/**
 * Every string literal in the C++ sources, as the compiler would see them.
 *
 * Read from the source with its comments removed, so a fragment quoted in a
 * comment is not mistaken for one the compiler emits.
 */
function stringLiteralsOf(sources: readonly ProgramSource[]): string[] {
  const literals = new Set<string>();
  for (const source of sources) {
    const { withoutComments } = lexSource(readFileSync(source.absolutePath, 'utf8'), source.family);
    for (const match of withoutComments.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)) {
      literals.add(match[1]);
    }
  }
  return [...literals];
}

/**
 * Stand-ins for the parameter name and value a message carries. Splitting a
 * rendered message on them leaves exactly the text the C++ side writes out as
 * string literals around its own concatenations.
 */
const PLACEHOLDERS = ['<parameter-name>', '<parameter-value>'] as const;

/** The fixed text of a message, split at the values it interpolates. */
function literalFragments(build: (...args: string[]) => string): string[] {
  return build(...PLACEHOLDERS)
    .split(new RegExp(PLACEHOLDERS.join('|')))
    .filter((fragment) => fragment.length > 0);
}

/** Which refusals this module can produce that the C++ pool does not spell. */
function unmatchedFragments(
  refusals: Record<string, (...args: string[]) => string>,
  cppLiterals: readonly string[],
): string[] {
  const unmatched: string[] = [];
  for (const [rule, build] of Object.entries(refusals)) {
    for (const fragment of literalFragments(build)) {
      if (!cppLiterals.includes(fragment)) {
        unmatched.push(`${rule}: ${JSON.stringify(fragment)}`);
      }
    }
  }
  return unmatched.sort();
}

describe('boundary acceptance rules', () => {
  const sources = cppSources();
  const cppLiterals = stringLiteralsOf(sources);

  it('reads the C++ sources', () => {
    // Guards the extraction: a pool built from nothing would make every
    // comparison below vacuously true, and so would one built from a single
    // file that happened to survive a rename.
    expect(sources.length).toBeGreaterThan(10);
    expect(cppLiterals.length).toBeGreaterThan(10);
    expect(Object.keys(boundaryAcceptanceError).length).toBeGreaterThan(0);
  });

  it('renders the integer step threshold into the message that states it', () => {
    expect(boundaryAcceptanceError.integerStep('n')).toContain(String(INTEGER_BOUNDARY_STEP));
  });

  it('names the metadata fields the same way the C++ sources do', () => {
    for (const field of BOUNDARY_METADATA_FIELDS) {
      expect(cppLiterals, field).toContain(field);
    }
  });

  it('states every refusal in the same words as the C++ sources', () => {
    expect(unmatchedFragments(boundaryAcceptanceError, cppLiterals)).toEqual([]);
  });
});

describe('the comparison reports wording that exists on one side only', () => {
  const refusals = {
    unknownParameter: (name: string): string => `Unknown parameter in boundary config: ${name}`,
    integerStep: (name: string): string => `Integer boundary step must be 1 for parameter ${name}`,
  };
  const spelledInCpp = [
    'Unknown parameter in boundary config: ',
    'Integer boundary step must be 1 for parameter ',
  ];

  it('accepts wording both sides spell the same way', () => {
    expect(unmatchedFragments(refusals, spelledInCpp)).toEqual([]);
  });

  it('reports a refusal the C++ sources do not spell at all', () => {
    const reworded = spelledInCpp.filter(
      (literal) => !literal.startsWith('Unknown parameter in boundary config'),
    );
    expect(unmatchedFragments(refusals, reworded)).toEqual([
      'unknownParameter: "Unknown parameter in boundary config: "',
    ]);
  });

  it('reports a reword that changes only the C++ side', () => {
    const reworded = spelledInCpp.map((literal) =>
      literal.replace('Unknown parameter in', 'Unrecognised parameter in'),
    );
    expect(unmatchedFragments(refusals, reworded)).toEqual([
      'unknownParameter: "Unknown parameter in boundary config: "',
    ]);
  });

  it('reports a threshold that moved on one side only', () => {
    // The number renders into the text, so a step the two sides disagree about
    // is a wording difference and has to be reported as one.
    const reworded = spelledInCpp.map((literal) => literal.replace('must be 1', 'must be 2'));
    expect(unmatchedFragments(refusals, reworded)).toEqual([
      'integerStep: "Integer boundary step must be 1 for parameter "',
    ]);
  });
});
