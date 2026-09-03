import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { describe, expect, it } from 'vitest';
import {
  discoverSources,
  isTestPath,
  lexSource,
  type ProgramSource,
  REPO_ROOT,
} from '../../../tests/util/source-scan.js';

/**
 * The C++ and TypeScript limit lists are two files, so nothing at build time
 * makes one follow the other. This test is that mechanism: it parses each
 * header and compares every constant, mapping kFooBar <-> FOO_BAR, and fails if
 * either side gains, loses, or changes an entry.
 *
 * The parse is deliberately blind to the declared type and reads whatever the
 * header declares, because a check that recognises only the types it was
 * written against reports full coverage while silently ignoring every constant
 * it does not recognise -- worse than no check, since it looks like one. What it
 * does insist on is that the value survives the crossing: a C++ integer wider
 * than a JavaScript number has no faithful mirror, so it is rejected here rather
 * than mirrored wrongly.
 *
 * Which headers are compared is not a list either. A list has to be extended by
 * hand, so a third header of constants -- or one that moved between layers --
 * stays outside the comparison while the mechanism goes on reporting full
 * agreement, and that is the same blind spot as a parse that recognises only
 * some declarations. The pairs are derived instead: the pure port mirrors the
 * engine layer for layer, so a C++ header in a mirrored layer that declares
 * constants has a module that must state the same ones, and a header that has
 * no such module is reported rather than skipped.
 */

/** A C++ header of constants and the module the pure port mirrors it with. */
interface MirroredHeader {
  header: ProgramSource;
  /** Path from the repository root of the module that must mirror it. */
  modulePath: string;
}

/** Convert a C++ `kMaxStringBytes` identifier to its TypeScript spelling. */
function toScreamingSnake(name: string): string {
  return name
    .replace(/^k/, '')
    .replace(/([a-z0-9])([A-Z])/g, '$1_$2')
    .toUpperCase();
}

/**
 * The layers the pure port mirrors, read from the port itself.
 *
 * A layer the port does not implement -- the binding, for one -- has no module
 * to compare against and no constants that need to cross, so a header there is
 * out of scope. Adding the layer to the port brings its headers into scope
 * without anything here changing.
 */
function mirroredLayers(): Set<string> {
  return new Set(
    discoverSources()
      .programs.filter((source) => source.relativePath.startsWith(`src${path.sep}ts${path.sep}`))
      .map((source) => source.relativePath.split(path.sep)[2]),
  );
}

/**
 * Every C++ header in a mirrored layer that declares constants, with the module
 * that has to state the same ones.
 *
 * The module's path follows from the header's: the port keeps the layer and the
 * name, spelled the way each language spells a name.
 */
function mirroredHeaders(): MirroredHeader[] {
  const layers = mirroredLayers();
  const found: MirroredHeader[] = [];
  for (const source of discoverSources().programs) {
    const segments = source.relativePath.split(path.sep);
    if (segments.length !== 3 || segments[0] !== 'src' || !layers.has(segments[1])) {
      continue;
    }
    if (source.extension !== 'h' || isTestPath(source.relativePath)) {
      continue;
    }
    const text = lexSource(
      readFileSync(source.absolutePath, 'utf8'),
      source.family,
    ).withoutComments;
    if (!/inline constexpr /.test(text)) {
      continue;
    }
    const moduleName = `${segments[2].replace(/\.h$/, '').replace(/_/g, '-')}.ts`;
    found.push({ header: source, modulePath: path.join('src', 'ts', segments[1], moduleName) });
  }
  return found.sort((left, right) =>
    left.header.relativePath.localeCompare(right.header.relativePath),
  );
}

/**
 * Evaluate the restricted arithmetic the headers use.
 *
 * A factor is a decimal literal, which may carry C++ digit separators and an
 * integer suffix (`16'000'000`, `1024u`), or the name of a constant declared
 * earlier in the same header, so a limit expressed as a multiple of another
 * (`4 * kMaxSearchNodes`) is read as the multiple it is rather than collapsing
 * to NaN. Anything else throws naming the factor it could not read: a limit this
 * cannot evaluate must stop the suite, not pass through as a number that
 * compares unequal to everything.
 */
function evaluateLiteral(
  name: string,
  expression: string,
  resolved: ReadonlyMap<string, number>,
): number {
  return expression
    .split('*')
    .map((rawFactor) => {
      const factor = rawFactor.trim();
      const literal = factor.replace(/'/g, '').replace(/[uUlL]+$/, '');
      if (/^\d+$/.test(literal)) {
        return Number(literal);
      }
      const referenced = resolved.get(factor);
      if (referenced !== undefined) {
        return referenced;
      }
      throw new Error(
        `Cannot evaluate '${factor}' in the definition of ${name}: a factor must be a decimal literal or a constant declared earlier in the header.`,
      );
    })
    .reduce((product, factor) => product * factor, 1);
}

/**
 * Read every constant a header declares, whatever its type.
 *
 * Counted independently of the parse as well, so a declaration written in a
 * shape the parse does not recognise is refused here rather than dropped,
 * leaving the comparison reporting an agreement it never checked. Values are
 * kept under both spellings while parsing so a later definition can refer to an
 * earlier one by its C++ name.
 */
function parseHeaderConstants(text: string): Map<string, number> {
  const found = new Map<string, number>();
  const byCppName = new Map<string, number>();
  for (const match of text.matchAll(/inline constexpr \w+ (k\w+) = ([^;]+);/g)) {
    const cppName = match[1];
    const value = evaluateLiteral(cppName, match[2], byCppName);
    byCppName.set(cppName, value);
    found.set(toScreamingSnake(cppName), value);
  }
  const declared = text.match(/^[ \t]*inline constexpr /gm)?.length ?? 0;
  if (declared !== found.size) {
    throw new Error(
      `The header declares ${declared} constants but ${found.size} were read; one is written in a shape this cannot parse.`,
    );
  }
  return found;
}

/** The numbers a module states, which is what a header's constants cross into. */
function numericExports(module: Record<string, unknown>): Map<string, number> {
  return new Map(
    Object.entries(module).filter(
      (entry): entry is [string, number] => typeof entry[1] === 'number',
    ),
  );
}

/**
 * Everywhere a header and its module disagree, named one by one.
 *
 * Reported rather than thrown at the first difference: a renamed constant is a
 * loss on one side and a gain on the other, and saying only one of those sends
 * the reader looking for a change that was never made.
 */
function disagreements(
  headerConstants: ReadonlyMap<string, number>,
  moduleNumbers: ReadonlyMap<string, number>,
): string[] {
  const found: string[] = [];
  for (const [name, value] of headerConstants) {
    if (!moduleNumbers.has(name)) {
      found.push(`${name} is declared in C++ and stated nowhere in TypeScript`);
      continue;
    }
    const mirrored = moduleNumbers.get(name) as number;
    if (mirrored !== value) {
      found.push(`${name} is ${value} in C++ and ${mirrored} in TypeScript`);
    }
  }
  for (const name of moduleNumbers.keys()) {
    if (!headerConstants.has(name)) {
      found.push(`${name} is stated in TypeScript and declared nowhere in C++`);
    }
  }
  return found.sort();
}

const pairs = mirroredHeaders();

const mirrors = await Promise.all(
  pairs.map(async (pair) => {
    const absolute = path.join(REPO_ROOT, pair.modulePath);
    return {
      ...pair,
      exists: existsSync(absolute),
      module: existsSync(absolute)
        ? ((await import(pathToFileURL(absolute).href)) as Record<string, unknown>)
        : {},
    };
  }),
);

describe('the mirrored constants', () => {
  it('finds every header of constants the pure port has a layer for', () => {
    // Guards the derivation: a pairing that found nothing, or that collapsed to
    // one layer after a move, would make every comparison below vacuous.
    expect(pairs.length).toBeGreaterThan(1);
    const layers = new Set(pairs.map((pair) => pair.header.relativePath.split(path.sep)[1]));
    expect(layers.size).toBeGreaterThan(1);
  });

  it('has a module for every one of them', () => {
    // A header of engine constants with no module beside it is a set of values
    // stated once. Naming it here is what stops it from being invisible: the
    // comparison it would have failed is one that never ran.
    expect(mirrors.filter((mirror) => !mirror.exists).map((mirror) => mirror.modulePath)).toEqual(
      [],
    );
  });
});

describe.each(mirrors)('$header.relativePath', ({ header, module }) => {
  const headerConstants = parseHeaderConstants(
    lexSource(readFileSync(header.absolutePath, 'utf8'), header.family).withoutComments,
  );

  it('parses every constant out of the C++ header', () => {
    expect(headerConstants.size).toBeGreaterThan(0);
  });

  it('parses every constant into a value TypeScript can hold', () => {
    // A C++ integer wider than 2^53 cannot be mirrored by a JavaScript number,
    // so it has to fail here: mirroring it would put a silently rounded value on
    // the TypeScript surfaces and the comparison below would pass.
    for (const [name, value] of headerConstants) {
      expect({ [name]: Number.isSafeInteger(value) }).toEqual({ [name]: true });
    }
  });

  it('states the same constants, with the same values, on both sides', () => {
    expect(disagreements(headerConstants, numericExports(module))).toEqual([]);
  });
});

describe('the comparison reports constants that have drifted', () => {
  const header = (): Map<string, number> =>
    new Map([
      ['MAX_PARAMETERS', 1024],
      ['MAX_STRING_BYTES', 65536],
    ]);
  const module = (): Map<string, number> =>
    new Map([
      ['MAX_PARAMETERS', 1024],
      ['MAX_STRING_BYTES', 65536],
    ]);

  it('accepts two sides that agree', () => {
    expect(disagreements(header(), module())).toEqual([]);
  });

  it('reports a limit renumbered on the C++ side alone', () => {
    const drifted = header();
    drifted.set('MAX_STRING_BYTES', 32768);
    expect(disagreements(drifted, module())).toEqual([
      'MAX_STRING_BYTES is 32768 in C++ and 65536 in TypeScript',
    ]);
  });

  it('reports a constant the pure port was never told about', () => {
    const drifted = header();
    drifted.set('MAX_DOCUMENT_BYTES', 1024);
    expect(disagreements(drifted, module())).toEqual([
      'MAX_DOCUMENT_BYTES is declared in C++ and stated nowhere in TypeScript',
    ]);
  });

  it('reports a constant the pure port invented for itself', () => {
    const drifted = module();
    drifted.set('MAX_DOCUMENT_BYTES', 1024);
    expect(disagreements(header(), drifted)).toEqual([
      'MAX_DOCUMENT_BYTES is stated in TypeScript and declared nowhere in C++',
    ]);
  });

  it('reports a rename from both sides at once', () => {
    const drifted = header();
    drifted.delete('MAX_STRING_BYTES');
    drifted.set('MAX_VALUE_BYTES', 65536);
    expect(disagreements(drifted, module())).toEqual([
      'MAX_STRING_BYTES is stated in TypeScript and declared nowhere in C++',
      'MAX_VALUE_BYTES is declared in C++ and stated nowhere in TypeScript',
    ]);
  });
});

describe('the parse refuses a header it could only partly read', () => {
  it('reads a header written the way the engine writes one', () => {
    const constants = parseHeaderConstants(
      [
        'inline constexpr size_t kMaxStringBytes = 64 * 1024;',
        "inline constexpr uint64_t kMaxTuples = 16'000'000;",
        'inline constexpr uint64_t kMaxClassTuples = 4 * kMaxTuples;',
      ].join('\n'),
    );
    expect([...constants]).toEqual([
      ['MAX_STRING_BYTES', 65536],
      ['MAX_TUPLES', 16000000],
      ['MAX_CLASS_TUPLES', 64000000],
    ]);
  });

  it('refuses a declaration written in a shape it cannot read', () => {
    // A constant whose name does not begin with `k`, or whose value is not the
    // arithmetic the headers use, would otherwise be dropped -- and a dropped
    // constant is one the comparison reports agreement about.
    expect(() =>
      parseHeaderConstants(
        [
          'inline constexpr size_t kMaxParameters = 1024;',
          'inline constexpr size_t MaxValues = 16384;',
        ].join('\n'),
      ),
    ).toThrow(/shape this cannot parse/);
  });

  it('refuses a value it cannot evaluate rather than reading it as nothing', () => {
    expect(() =>
      parseHeaderConstants('inline constexpr size_t kMaxStringBytes = kSomethingElse * 2;'),
    ).toThrow(/must be a decimal literal or a constant declared earlier/);
  });
});
