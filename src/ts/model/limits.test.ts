import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import * as limits from './limits.js';
import * as tuningLimits from './tuning-limits.js';

/**
 * The C++ and TypeScript limit lists are two files, so nothing at build time
 * makes one follow the other. This test is that mechanism: it parses the header
 * and compares every constant, mapping kFooBar <-> FOO_BAR, and fails if either
 * side gains, loses, or changes an entry.
 *
 * The parse is deliberately blind to the declared type and reads whatever the
 * header declares, because a check that recognises only the types it was
 * written against reports full coverage while silently ignoring every constant
 * it does not recognise -- worse than no check, since it looks like one. What
 * it does insist on is that the value survives the crossing: a C++ integer
 * wider than a JavaScript number has no faithful mirror, so it is rejected here
 * rather than mirrored wrongly.
 */

/**
 * Each C++ header and the TypeScript module that mirrors it. The constants are
 * split by whether they are part of what coverwise accepts or of how it is
 * tuned, and the check has to follow that split: a parity check pointed at one
 * header while constants live in two is the same blind spot as a parse that
 * recognises only some of them.
 */
const MIRRORED_HEADERS = [
  { header: 'limits.h', module: limits },
  { header: 'tuning_limits.h', module: tuningLimits },
] as const;

function headerPathOf(header: string): string {
  return path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../model', header);
}

/** Convert a C++ `kMaxStringBytes` identifier to its TypeScript spelling. */
function toScreamingSnake(name: string): string {
  return name
    .replace(/^k/, '')
    .replace(/([a-z0-9])([A-Z])/g, '$1_$2')
    .toUpperCase();
}

/**
 * Evaluate the restricted arithmetic the header uses.
 *
 * A factor is a decimal literal, which may carry C++ digit separators and an
 * integer suffix (`16'000'000`, `1024u`), or the name of a constant declared
 * earlier in the same header, so a limit expressed as a multiple of another
 * (`4 * kMaxSearchNodes`) is read as the multiple it is rather than collapsing
 * to NaN. Anything else throws naming the factor it could not read: a limit
 * this cannot evaluate must stop the suite, not pass through as a number that
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
 * Read every constant the header declares, whatever its type.
 *
 * Values are keyed by both spellings while parsing so a later definition can
 * refer to an earlier one by its C++ name.
 */
function parseHeaderConstants(headerPath: string): Map<string, number> {
  const source = readFileSync(headerPath, 'utf8');
  const pattern = /inline constexpr \w+ (k\w+) = ([^;]+);/g;
  const found = new Map<string, number>();
  const byCppName = new Map<string, number>();
  for (const match of source.matchAll(pattern)) {
    const cppName = match[1];
    const value = evaluateLiteral(cppName, match[2], byCppName);
    byCppName.set(cppName, value);
    found.set(toScreamingSnake(cppName), value);
  }
  return found;
}

/** How many constants the header declares, counted without parsing them. */
function countHeaderDeclarations(headerPath: string): number {
  return readFileSync(headerPath, 'utf8').match(/^\s*inline constexpr /gm)?.length ?? 0;
}

describe.each(MIRRORED_HEADERS)('$header', ({ header, module }) => {
  const headerPath = headerPathOf(header);
  const headerConstants = parseHeaderConstants(headerPath);

  it('parses every constant out of the C++ header', () => {
    expect(headerConstants.size).toBeGreaterThan(0);

    // Counted independently of the parse, so a declaration written in a shape
    // the parse does not recognise is caught here rather than being dropped and
    // leaving the comparison below reporting agreement it never checked.
    expect(headerConstants.size).toBe(countHeaderDeclarations(headerPath));
  });

  it('parses every constant into a value TypeScript can hold', () => {
    // A C++ integer wider than 2^53 cannot be mirrored by a JavaScript number,
    // so it has to fail here: mirroring it would put a silently rounded value
    // on the TypeScript surfaces and the comparison below would pass.
    for (const [name, value] of headerConstants) {
      expect({ [name]: Number.isSafeInteger(value) }).toEqual({ [name]: true });
    }
  });

  it('declares the same constant names on both sides', () => {
    expect([...headerConstants.keys()].sort()).toEqual(Object.keys(module).sort());
  });

  it('agrees on every value', () => {
    for (const [name, value] of headerConstants) {
      expect({ [name]: (module as Record<string, number>)[name] }).toEqual({ [name]: value });
    }
  });
});
