import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

/**
 * A structured error carries its message and its detail as two separate public
 * fields, so joining them into the text a user reads is something any file in
 * the tree can write. Written twice, the two spellings drift: one call site
 * leaves a separator behind when the detail is empty, another drops the detail
 * entirely, and the same failure reads differently depending on which surface
 * reported it.
 *
 * This is the mechanism that keeps that composition single. It walks the engine
 * sources in both languages and fails on every read of an error's message or
 * detail outside the two renderings -- `SurfaceError` in C++ and
 * `surfaceErrorText` in TypeScript. The rule is deny-by-default: a read is an
 * offence unless it matches one of the two shapes below, which are shapes
 * rather than a list of forgiven files, because such a list has to be extended
 * by hand and is therefore only as good as the memory of whoever writes the
 * next call site.
 *
 * The scan reads the source tree rather than a compiled symbol because the
 * constraint is about where display text may be built, and neither language
 * expresses that in its type system while the fields are public.
 */

const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../..');

/** Everything the engine ships from, in both languages. */
const SCAN_ROOTS = ['src', 'tools', 'js'];

const SOURCE_EXTENSIONS = ['cpp', 'h', 'ts'];

/** The one rendering per language, and the only place a join may live. */
const TEXT_MAPPINGS = ['src/model/surface_error.h', 'src/ts/model/error.ts'];

/**
 * A read of either half of an error.
 *
 * The lookahead lets an assignment through: `err.message = "..."` builds an
 * error rather than rendering one. A comparison is a read and stays caught.
 */
const READ_MOTIF = /\.(?:message|detail)\b(?!\s*=[^=])/;

/** Two halves joined into one string, in either language's spelling. */
const JOIN_MOTIF = /[+`]|\$\{/;

/** Both halves read on one line, with whatever stands between them. */
const PAIRED_READS = /\.(?:message|detail)\b(.*?)\.(?:message|detail)\b/;

/** Drop line comments, so prose about an error's detail is not read as code. */
function codeOf(line: string): string {
  const comment = line.indexOf('//');
  return comment === -1 ? line : line.slice(0, comment);
}

/**
 * Extending an error's own message keeps it an error.
 *
 * The text is still headed for the rendering, which will add the detail as it
 * always does, so nothing about how the failure is displayed is decided here. A
 * line that reads the detail is not this shape: composing the two halves is
 * exactly what may not happen outside the rendering.
 */
function isMessageAnnotation(code: string): boolean {
  const assignsMessage = /\.message\s*=[^=]|\bmessage:\s/.test(code);
  return assignsMessage && !/\.detail\b/.test(code);
}

/**
 * Reading the message of a JavaScript builtin `Error`.
 *
 * A caught exception is not one of the engine's structured errors: it has no
 * detail to lose and no rendering to go through. The guard has to stand in the
 * same expression as the read, so this cannot cover a structured error that
 * merely happens to sit near a type test.
 */
function isBuiltinErrorRead(code: string): boolean {
  return /instanceof Error\b/.test(code) && !/\.detail\b/.test(code);
}

/**
 * Handing both halves across a surface boundary as separate values.
 *
 * The receiving side still holds two fields and still decides how to render
 * them, so no display text is built here. The two reads have to be separate
 * arguments: anything joining them is the composition this check exists to
 * catch, so a `+`, a template placeholder or a backtick between them
 * disqualifies the hand-off.
 */
function isUnjoinedHandoff(statement: string): boolean {
  const pair = statement.match(PAIRED_READS);
  if (pair === null) {
    return false;
  }
  const between = pair[1];
  return between.includes(',') && !JOIN_MOTIF.test(between);
}

/**
 * The statement each line belongs to.
 *
 * An argument list the formatter broke across lines is one expression, and
 * judging its lines separately would read a hand-off that spans four lines as
 * four lone reads. Statements end at `;` or at a brace, which is as much syntax
 * as this needs to know.
 */
function statementTexts(codeLines: string[]): string[] {
  const texts = new Array<string>(codeLines.length);
  let start = 0;
  for (let index = 0; index < codeLines.length; index += 1) {
    if (!/[;{}]/.test(codeLines[index]) && index + 1 < codeLines.length) {
      continue;
    }
    const statement = codeLines.slice(start, index + 1).join(' ');
    for (let inner = start; inner <= index; inner += 1) {
      texts[inner] = statement;
    }
    start = index + 1;
  }
  return texts;
}

function engineSources(directory: string): string[] {
  const scannable = new RegExp(`\\.(${SOURCE_EXTENSIONS.join('|')})$`);
  const found: string[] = [];
  for (const entry of readdirSync(directory, { withFileTypes: true })) {
    const full = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      found.push(...engineSources(full));
    } else if (scannable.test(entry.name) && !entry.name.endsWith('.test.ts')) {
      found.push(full);
    }
  }
  return found;
}

describe('an error becomes text in one place', () => {
  const sources = SCAN_ROOTS.flatMap((root) => engineSources(path.join(REPO_ROOT, root))).filter(
    (file) =>
      !TEXT_MAPPINGS.some((mapping) => path.relative(REPO_ROOT, file) === path.normalize(mapping)),
  );

  it('finds the engine sources of both languages to scan', () => {
    expect(sources.length).toBeGreaterThan(10);
    for (const extension of SOURCE_EXTENSIONS) {
      expect(sources.some((file) => file.endsWith(`.${extension}`))).toBe(true);
    }
  });

  it('finds no source reading an error message or detail outside the rendering', () => {
    const offenders: string[] = [];
    for (const file of sources) {
      const lines = readFileSync(file, 'utf8').split('\n');
      const codeLines = lines.map(codeOf);
      const statements = statementTexts(codeLines);
      codeLines.forEach((code, index) => {
        if (!READ_MOTIF.test(code)) {
          return;
        }
        if (isMessageAnnotation(code) || isBuiltinErrorRead(code)) {
          return;
        }
        if (isUnjoinedHandoff(statements[index])) {
          return;
        }
        offenders.push(`${path.relative(REPO_ROOT, file)}:${index + 1}: ${lines[index].trim()}`);
      });
    }
    expect(offenders).toEqual([]);
  });

  it('finds the join inside each rendering', () => {
    for (const mapping of TEXT_MAPPINGS) {
      const joined = readFileSync(path.join(REPO_ROOT, mapping), 'utf8')
        .split('\n')
        .map(codeOf)
        .some(
          (code) => /\.message\b/.test(code) && /\.detail\b/.test(code) && JOIN_MOTIF.test(code),
        );
      expect({ [mapping]: joined }).toEqual({ [mapping]: true });
    }
  });
});
