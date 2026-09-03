import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import {
  discoverSources,
  isTestPath,
  lexSource,
  type ProgramSource,
} from '../../../tests/util/source-scan.js';
import {
  aggregateBudgetExceeded,
  CHARGED_STRING_KINDS,
  chargedStringContext,
  stringBudgetExceeded,
} from './budget.js';

/**
 * Each refusal is stated twice -- once here, once in C++ -- so nothing at build
 * time makes one follow the other. This rebuilds each C++ sentence from its own
 * source: the two quoted fragments of the definition, in the order it writes
 * them, with the byte figure resolved from the constant that definition reads
 * it from. Comparing the rebuilt sentence to this module's catches a reword on
 * either side, including one that changes only C++ -- which comparing two
 * renderings of the same TypeScript constant would not.
 *
 * The contexts those sentences carry are held together from the other end:
 * every fixed fragment a context can render has to appear among the literals
 * the C++ generator writes. A kind spelled differently on one side fails here
 * rather than reaching a caller comparing two surfaces' accounts of one string.
 *
 * The definitions and the constants are found by name across the C++ sources
 * rather than read out of files named here. Moving any of them is a rewrite
 * that changes nothing about the wording, and a check that had to be pointed at
 * the new location by hand would fail for a reason that has nothing to do with
 * what it guards -- or, worse, be repointed at a file that no longer holds it.
 */

const DEFINITION_NAME = 'AggregateBudgetExceededMessage';

/** The C++ definition of the per-string refusal. */
const STRING_DEFINITION_NAME = 'StringBudgetExceededMessage';

/** The C++ definition of the one context per charged kind. */
const CONTEXT_DEFINITION_NAME = 'ChargedStringContext';

/** Every C++ source the engine ships, found by where it is. */
function cppSources(): ProgramSource[] {
  return discoverSources().programs.filter(
    (source) => source.family === 'c-like' && !isTestPath(source.relativePath),
  );
}

/** The C++ sources with their comments removed, keyed by path. */
function cppTexts(sources: readonly ProgramSource[]): Map<string, string> {
  const texts = new Map<string, string>();
  for (const source of sources) {
    texts.set(
      source.relativePath,
      lexSource(readFileSync(source.absolutePath, 'utf8'), source.family).withoutComments,
    );
  }
  return texts;
}

/**
 * The one match of @p pattern across the sources.
 *
 * Anything other than exactly one is a refusal rather than a guess: none means
 * the thing being compared against is gone, and two means the comparison would
 * have picked whichever came first.
 */
function theOne(texts: ReadonlyMap<string, string>, pattern: string, what: string): string {
  const found: string[] = [];
  for (const text of texts.values()) {
    for (const match of text.matchAll(new RegExp(pattern, 'g'))) {
      found.push(match[1]);
    }
  }
  if (found.length !== 1) {
    throw new Error(`Expected exactly one ${what} in the C++ sources, found ${found.length}.`);
  }
  return found[0];
}

/** The body of a C++ definition returning a std::string, wherever it is written. */
function messageBody(texts: ReadonlyMap<string, string>, name = DEFINITION_NAME): string {
  return theOne(
    texts,
    `std::string ${name}\\([^)]*\\)\\s*\\{([\\s\\S]*?)\\n\\}`,
    `definition of ${name}`,
  );
}

/** Every string literal a C++ definition writes out. */
function bodyLiterals(body: string): string[] {
  return [...body.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)].map((match) => match[1]);
}

/** A `constexpr` constant declared anywhere in the C++ sources, by its name. */
function cppConstant(texts: ReadonlyMap<string, string>, name: string): number {
  return theOne(texts, `inline constexpr \\w+ ${name} = ([^;]+);`, `declaration of ${name}`)
    .split('*')
    .map((factor) => Number(factor.trim()))
    .reduce((product, factor) => product * factor, 1);
}

/**
 * The sentence the C++ helper returns.
 *
 * Its body is `"..." + std::to_string(<constant>) + "..."`, so the fragments in
 * source order joined around that constant's value are the whole message. The
 * constant is named unqualified there -- the function sits inside
 * `namespace model` -- and resolving it by that name is exact as long as one
 * declaration answers to it, which is asserted rather than assumed.
 */
function cppMessage(texts: ReadonlyMap<string, string>, name = DEFINITION_NAME): string {
  const body = messageBody(texts, name);
  const fragments = bodyLiterals(body);
  const constant = body.match(/std::to_string\((\w+)\)/);
  if (fragments.length !== 2 || constant === null) {
    throw new Error(
      `${name} is not "<text>" + std::to_string(<constant>) + "<text>"; the rebuild cannot stand for it.`,
    );
  }
  return fragments[0] + String(cppConstant(texts, constant[1])) + fragments[1];
}

/**
 * Stand-ins for the subject and position a context interpolates. Splitting a
 * rendered context on them leaves exactly the text the C++ side writes out as
 * string literals around its own concatenations.
 */
const SUBJECT = '<subject>';
const INDEX = 1234567;

/** The fixed text of a context, split at the values it interpolates. */
function contextFragments(build: (...args: unknown[]) => string): string[] {
  return build(SUBJECT, INDEX)
    .split(new RegExp(`${SUBJECT}|${INDEX}`))
    .filter((fragment) => fragment.length > 0);
}

describe('the aggregate budget refusal', () => {
  const texts = cppTexts(cppSources());

  it('finds the C++ definition wherever it is written', () => {
    expect(texts.size).toBeGreaterThan(10);
    expect(messageBody(texts)).toContain('Input strings exceed');
  });

  it('is worded exactly as the C++ model layer words it', () => {
    expect(aggregateBudgetExceeded()).toBe(cppMessage(texts));
  });
});

describe('the per-string budget refusal', () => {
  const texts = cppTexts(cppSources());

  it('finds the C++ definition wherever it is written', () => {
    expect(messageBody(texts, STRING_DEFINITION_NAME)).toContain('exceeds');
  });

  // Both sides take the context as an argument, so what is compared is the
  // sentence written around it.
  it('is worded exactly as the C++ model layer words it', () => {
    expect(stringBudgetExceeded('')).toBe(cppMessage(texts, STRING_DEFINITION_NAME));
  });

  it('reports the string it was asked about', () => {
    expect(stringBudgetExceeded('<context>')).toContain('<context>');
  });
});

describe('the charged set', () => {
  const texts = cppTexts(cppSources());
  const literals = bodyLiterals(messageBody(texts, CONTEXT_DEFINITION_NAME));

  it('finds the C++ generator wherever it is written', () => {
    expect(literals.length).toBeGreaterThan(0);
  });

  // A kind on the list with no context is a kind no refusal can name, and one
  // in the map that is not on the list is a spelling nothing charges.
  it('gives every kind one context and no more', () => {
    expect(Object.keys(chargedStringContext).sort()).toEqual([...CHARGED_STRING_KINDS].sort());
  });

  for (const kind of CHARGED_STRING_KINDS) {
    it(`names ${kind} in the same words as the C++ generator`, () => {
      const build = chargedStringContext[kind] as (...args: unknown[]) => string;
      for (const fragment of contextFragments(build)) {
        expect(literals, `${kind}: ${JSON.stringify(fragment)}`).toContain(fragment);
      }
    });
  }
});

describe('the rebuild reports a definition that has changed', () => {
  const wording: [string, string] = ['Input strings exceed the ', ' byte budget.'];

  /** A C++ source holding one definition and the constant it interpolates. */
  const definitionOf = (fragments: [string, string], bytes: number): string =>
    [
      `inline constexpr size_t kFixtureBudget = ${bytes};`,
      `std::string ${DEFINITION_NAME}() {`,
      `  return "${fragments[0]}" + std::to_string(kFixtureBudget) + "${fragments[1]}";`,
      '}',
    ].join('\n');

  const sourceWith = (fragments: [string, string], bytes: number): Map<string, string> =>
    new Map([['fixture.cpp', definitionOf(fragments, bytes)]]);

  it('rebuilds the sentence a definition writes', () => {
    expect(cppMessage(sourceWith(wording, 4096))).toBe(
      'Input strings exceed the 4096 byte budget.',
    );
  });

  it('reports a reword that changes only the C++ side', () => {
    expect(cppMessage(sourceWith(['Input strings are over the ', ' byte budget.'], 4096))).not.toBe(
      cppMessage(sourceWith(wording, 4096)),
    );
  });

  it('reports a budget renumbered on the C++ side alone', () => {
    // The figure is not in the sentence's own source: rebuilding without
    // resolving the constant would compare two sentences that agree about
    // everything except the number a caller is told.
    expect(cppMessage(sourceWith(wording, 8192))).not.toBe(cppMessage(sourceWith(wording, 4096)));
  });

  it('refuses a definition the rebuild cannot stand for', () => {
    // A definition that stopped being two fragments around one constant would
    // otherwise be rebuilt into something shorter than what it returns, and the
    // comparison would go on reporting agreement.
    const assembled = new Map([
      [
        'fixture.cpp',
        [
          `std::string ${DEFINITION_NAME}() {`,
          '  std::string text = "Input strings exceed the ";',
          '  text += std::to_string(kFixtureBudget);',
          '  text += " byte budget";',
          '  text += ".";',
          '  return text;',
          '}',
        ].join('\n'),
      ],
    ]);
    expect(() => cppMessage(assembled)).toThrow(/cannot stand for it/);
  });

  it('refuses to guess when the definition is gone', () => {
    expect(() => cppMessage(new Map([['fixture.cpp', 'int main() { return 0; }']]))).toThrow(
      /found 0/,
    );
  });

  it('refuses to guess when two definitions answer to the name', () => {
    const twice = new Map([
      ['fixture.cpp', definitionOf(wording, 4096)],
      ['other.cpp', definitionOf(wording, 4096)],
    ]);
    expect(() => cppMessage(twice)).toThrow(/found 2/);
  });
});
