import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

/**
 * Discovery and lexical reading shared by the mechanisms that scan sources.
 *
 * A scan whose reach is a list of directories covers what whoever wrote the
 * list remembered, and nothing that arrived afterwards: a new binding, a new
 * language, a file moved one level up. The reach here is decided by where a
 * file is instead. Everything the repository holds is walked, everything the
 * repository ignores is not, and an extension belonging to neither the program
 * languages nor the data formats is reported rather than quietly dropped -- so
 * a language nobody anticipated stops the suite instead of passing through it.
 *
 * The lexer exists for the same reason. A scan that finds its motifs in raw
 * text reads a URL in a comment as code and a comment marker inside a string
 * literal as the start of a comment, and both are notations an author writes
 * without thinking about the scan at all.
 */

export const REPO_ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');

/**
 * The comment and string syntax a file is written in.
 *
 * The ECMAScript family is separated from C and C++ because it has two
 * notations they do not: the template literal, whose backtick is what a join
 * looks like, and the regular-expression literal, whose contents include quote
 * and comment characters that mean nothing there.
 */
export type LexicalFamily = 'c-like' | 'js-like' | 'hash-like';

/**
 * Program text, by extension.
 *
 * The family decides how a file is read, not whether it is read: every program
 * language in the repository is scanned, including ones no mechanism has a
 * motif for yet.
 */
const PROGRAM_EXTENSIONS = new Map<string, LexicalFamily>([
  ['c', 'c-like'],
  ['cc', 'c-like'],
  ['cpp', 'c-like'],
  ['h', 'c-like'],
  ['hpp', 'c-like'],
  ['inc', 'c-like'],
  ['js', 'js-like'],
  ['cjs', 'js-like'],
  ['mjs', 'js-like'],
  ['ts', 'js-like'],
  ['tsx', 'js-like'],
  ['bash', 'hash-like'],
  ['py', 'hash-like'],
  ['sh', 'hash-like'],
]);

/**
 * Formats that carry no program text.
 *
 * Listed so that an extension in neither set is an unclassified one. A build
 * manifest and a document cannot compose an error message; a source file in a
 * language added tomorrow can, which is the case this partition separates out.
 */
const DATA_EXTENSIONS = new Set([
  '',
  'cmake',
  'in',
  'json',
  'lock',
  'md',
  'svg',
  'toml',
  'txt',
  'typed',
  'yaml',
  'yml',
]);

/** A file the scan reads, with the syntax needed to read it. */
export interface ProgramSource {
  /** Path from the repository root, in the platform's separator. */
  relativePath: string;
  absolutePath: string;
  extension: string;
  family: LexicalFamily;
}

export interface Discovery {
  /** Every program source the repository holds, tests included. */
  programs: ProgramSource[];
  /** Files whose extension belongs to neither partition, by relative path. */
  unclassified: string[];
}

/** One `.gitignore` line, as a match against a repository-relative path. */
interface IgnoreRule {
  negated: boolean;
  directoryOnly: boolean;
  pattern: RegExp;
}

/**
 * Translate a `.gitignore` glob into an anchored regular expression.
 *
 * Bracket expressions carry over as they are, which is what they mean in both
 * notations. An unbalanced bracket produces an invalid expression and throws,
 * which is the direction to fail in: a pattern silently read as something else
 * would drop files from the scan.
 */
function globToRegExp(glob: string): RegExp {
  let source = '';
  for (let index = 0; index < glob.length; index += 1) {
    const char = glob[index];
    if (char === '*') {
      if (glob[index + 1] === '*') {
        source += '.*';
        index += 1;
      } else {
        source += '[^/]*';
      }
      continue;
    }
    if (char === '?') {
      source += '[^/]';
      continue;
    }
    source += char.replace(/[.+^${}()|\\]/, '\\$&');
  }
  return new RegExp(`^${source}$`);
}

/**
 * The repository's own ignore rules.
 *
 * Build trees, vendored dependencies and generated output leave the scan
 * because the repository ignores them, not because a second list here repeats
 * them. Nothing can be dropped from the scan without also being dropped from
 * the repository.
 *
 * The subset of the format the file uses: comments, negations, a trailing
 * slash for directory-only, a leading slash or an interior slash for a rule
 * anchored at the root, and otherwise a match against a name at any depth. Only
 * the root file is read, so a rule written deeper leaves its files in the scan
 * rather than out of it -- the direction that reports too much rather than too
 * little.
 */
function parseIgnoreRules(): IgnoreRule[] {
  const ignorePath = path.join(REPO_ROOT, '.gitignore');
  if (!existsSync(ignorePath)) {
    return [];
  }
  const rules: IgnoreRule[] = [];
  for (const rawLine of readFileSync(ignorePath, 'utf8').split('\n')) {
    const line = rawLine.trim();
    if (line.length === 0 || line.startsWith('#')) {
      continue;
    }
    const negated = line.startsWith('!');
    let glob = negated ? line.slice(1) : line;
    const directoryOnly = glob.endsWith('/');
    glob = glob.replace(/\/$/, '');
    const anchored = glob.startsWith('/') || glob.includes('/');
    glob = glob.replace(/^\//, '');
    const body = globToRegExp(glob).source.replace(/^\^|\$$/g, '');
    rules.push({
      negated,
      directoryOnly,
      pattern: new RegExp(anchored ? `^${body}$` : `^(?:.*/)?${body}$`),
    });
  }
  return rules;
}

/** Whether the rules ignore a repository-relative path. Later rules win. */
function isIgnored(
  rules: readonly IgnoreRule[],
  relativePath: string,
  isDirectory: boolean,
): boolean {
  const posix = relativePath.split(path.sep).join('/');
  let ignored = false;
  for (const rule of rules) {
    if (rule.directoryOnly && !isDirectory) {
      continue;
    }
    if (rule.pattern.test(posix)) {
      ignored = !rule.negated;
    }
  }
  return ignored;
}

/**
 * Every program source in the repository.
 *
 * Dot-prefixed entries are tool and editor state rather than shipped source;
 * everything else is walked unless the repository ignores it, so a directory
 * added at any depth is scanned without being named anywhere.
 */
export function discoverSources(root: string = REPO_ROOT): Discovery {
  const rules = parseIgnoreRules();
  const programs: ProgramSource[] = [];
  const unclassified: string[] = [];

  const walk = (directory: string): void => {
    for (const entry of readdirSync(directory, { withFileTypes: true })) {
      if (entry.name.startsWith('.')) {
        continue;
      }
      const absolutePath = path.join(directory, entry.name);
      const relativePath = path.relative(REPO_ROOT, absolutePath);
      if (isIgnored(rules, relativePath, entry.isDirectory())) {
        continue;
      }
      if (entry.isDirectory()) {
        walk(absolutePath);
        continue;
      }
      if (!entry.isFile()) {
        continue;
      }
      const extension = path.extname(entry.name).replace(/^\./, '');
      const family = PROGRAM_EXTENSIONS.get(extension);
      if (family !== undefined) {
        programs.push({
          relativePath: path.relative(root, absolutePath),
          absolutePath,
          extension,
          family,
        });
      } else if (!DATA_EXTENSIONS.has(extension)) {
        unclassified.push(path.relative(root, absolutePath));
      }
    }
  };

  walk(root);
  programs.sort((left, right) => left.relativePath.localeCompare(right.relativePath));
  unclassified.sort();
  return { programs, unclassified };
}

/**
 * Whether a path holds test code.
 *
 * Recognised from the path itself rather than from a list of test directories,
 * in each language's own convention. Tests assert on the text a mechanism
 * guards, so scanning them would report every assertion as an offence.
 */
export function isTestPath(relativePath: string): boolean {
  const segments = relativePath.split(path.sep);
  const name = segments[segments.length - 1];
  if (segments.slice(0, -1).some((segment) => segment === 'test' || segment === 'tests')) {
    return true;
  }
  return /[._-]tests?\.[^.]+$/.test(name) || /^test_/.test(name) || name === 'conftest.py';
}

/** The two readings of a source file the motifs are matched against. */
export interface LexedSource {
  /**
   * Comments blanked, string literals left as written. What a motif needs when
   * it looks for a name spelled inside a literal, such as a bracket access.
   */
  withoutComments: string;
  /**
   * Comments and the contents of string literals both blanked, with the quotes
   * and the interpolation syntax left standing. What a motif needs when it
   * looks for code, since prose inside a literal is not code while a template's
   * backtick still marks a join.
   */
  code: string;
}

/**
 * Read a source file into the two views, blanking rather than deleting.
 *
 * Every blanked character becomes a space and every newline survives, so an
 * offset in either view is the offset in the original and a line number
 * reported from either view names the line the author wrote.
 *
 * An unterminated literal or comment throws. The alternative is a lexer that
 * runs off the end of a string and reads the rest of the file as literal text,
 * which is the one failure a deny-by-default scan cannot afford: it would
 * report agreement over source it never looked at.
 */
export function lexSource(text: string, family: LexicalFamily): LexedSource {
  return family === 'hash-like' ? lexHashLike(text) : lexBraced(text, family === 'js-like');
}

/** Mutable state shared by the two lexers while they blank a file. */
class Blanker {
  readonly withoutComments: string[];
  readonly code: string[];
  private readonly text: string;

  constructor(text: string) {
    this.text = text;
    this.withoutComments = [...text];
    this.code = [...text];
  }

  /** Blank a position in both views: a comment is neither code nor literal. */
  comment(index: number): void {
    this.blank(this.withoutComments, index);
    this.blank(this.code, index);
  }

  /** Blank a position in the code view alone: the inside of a literal. */
  literal(index: number): void {
    this.blank(this.code, index);
  }

  finish(): LexedSource {
    return { withoutComments: this.withoutComments.join(''), code: this.code.join('') };
  }

  private blank(view: string[], index: number): void {
    if (index < this.text.length && this.text[index] !== '\n') {
      view[index] = ' ';
    }
  }
}

/** An open template literal, or an interpolation opened inside one. */
type CLikeFrame = { kind: 'template' } | { kind: 'interpolation'; braces: number };

function unterminated(text: string, start: number, what: string): Error {
  const line = text.slice(0, start).split('\n').length;
  return new Error(`Unterminated ${what} at line ${line}; the source cannot be read as written.`);
}

/**
 * A digit separator, not the start of a character literal.
 *
 * `16'000'000` is how the limits are written, and reading the first quote as a
 * literal would swallow the rest of the declaration.
 */
function isDigitSeparator(text: string, index: number): boolean {
  return /\d/.test(text[index - 1] ?? '') && /\d/.test(text[index + 1] ?? '');
}

/**
 * Whether a slash at @p index opens a regular expression rather than dividing.
 *
 * Decided by what precedes it: a value can be divided, an operator or an
 * opening bracket cannot, so a slash after one of those begins a literal. The
 * distinction only has to hold for ECMAScript, which is the one family here
 * that has the notation at all.
 */
function opensRegExp(code: readonly string[], index: number): boolean {
  for (let before = index - 1; before >= 0; before -= 1) {
    const char = code[before];
    if (/\s/.test(char)) {
      continue;
    }
    return !/[\w$)\]}'"`]/.test(char);
  }
  return true;
}

/**
 * Blank a regular-expression literal and return the index after its flags.
 *
 * A character class may hold an unescaped slash, so the scan tracks whether it
 * is inside one; otherwise the first unescaped slash closes the literal.
 */
function blankRegExp(text: string, start: number, out: Blanker): number {
  let index = start + 1;
  let inClass = false;
  while (index < text.length) {
    const char = text[index];
    if (char === '\n') {
      throw unterminated(text, start, 'regular expression');
    }
    if (char === '\\') {
      out.literal(index);
      out.literal(index + 1);
      index += 2;
      continue;
    }
    if (char === '[') {
      inClass = true;
    } else if (char === ']') {
      inClass = false;
    } else if (char === '/' && !inClass) {
      index += 1;
      while (index < text.length && /[a-z]/.test(text[index])) {
        index += 1;
      }
      return index;
    }
    out.literal(index);
    index += 1;
  }
  throw unterminated(text, start, 'regular expression');
}

function lexBraced(text: string, ecmascript: boolean): LexedSource {
  const out = new Blanker(text);
  const stack: CLikeFrame[] = [];
  let index = 0;

  while (index < text.length) {
    const top = stack[stack.length - 1];
    const char = text[index];

    if (top?.kind === 'template') {
      if (char === '\\') {
        out.literal(index);
        out.literal(index + 1);
        index += 2;
        continue;
      }
      if (char === '`') {
        stack.pop();
        index += 1;
        continue;
      }
      if (char === '$' && text[index + 1] === '{') {
        stack.push({ kind: 'interpolation', braces: 0 });
        index += 2;
        continue;
      }
      out.literal(index);
      index += 1;
      continue;
    }

    if (top?.kind === 'interpolation' && (char === '{' || char === '}')) {
      if (char === '}' && top.braces === 0) {
        stack.pop();
      } else {
        top.braces += char === '{' ? 1 : -1;
      }
      index += 1;
      continue;
    }

    if (char === '/' && text[index + 1] === '/') {
      while (index < text.length && text[index] !== '\n') {
        out.comment(index);
        index += 1;
      }
      continue;
    }
    if (char === '/' && text[index + 1] === '*') {
      const end = text.indexOf('*/', index + 2);
      if (end === -1) {
        throw unterminated(text, index, 'block comment');
      }
      for (let position = index; position < end + 2; position += 1) {
        out.comment(position);
      }
      index = end + 2;
      continue;
    }
    if (char === '/' && ecmascript && opensRegExp(out.code, index)) {
      index = blankRegExp(text, index, out);
      continue;
    }
    if (char === '"' && !ecmascript && opensRawString(text, index)) {
      index = blankRawString(text, index, out);
      continue;
    }
    if (char === '`') {
      stack.push({ kind: 'template' });
      index += 1;
      continue;
    }
    if ((char === '"' || char === "'") && !(char === "'" && isDigitSeparator(text, index))) {
      index = blankQuoted(text, index, out, char, false);
      continue;
    }
    index += 1;
  }

  if (stack.length > 0) {
    throw unterminated(text, text.length, 'template literal');
  }
  return out.finish();
}

/**
 * Blank the inside of a quoted run and return the index after its closing
 * quote. A newline inside one is unterminated in every language here: the
 * multi-line forms are handled by their own callers.
 */
function blankQuoted(
  text: string,
  start: number,
  out: Blanker,
  quote: string,
  raw: boolean,
): number {
  let index = start + 1;
  while (index < text.length) {
    const char = text[index];
    if (char === '\n') {
      throw unterminated(text, start, 'string literal');
    }
    if (char === '\\' && !raw) {
      out.literal(index);
      out.literal(index + 1);
      index += 2;
      continue;
    }
    if (char === quote) {
      return index + 1;
    }
    out.literal(index);
    index += 1;
  }
  throw unterminated(text, start, 'string literal');
}

/** The string prefix letters immediately before a quote, lowercased. */
function quotePrefix(text: string, quoteIndex: number): string {
  const before = text.slice(Math.max(0, quoteIndex - 3), quoteIndex);
  return (/[A-Za-z]*$/.exec(before)?.[0] ?? '').toLowerCase();
}

function lexHashLike(text: string): LexedSource {
  const out = new Blanker(text);
  let index = 0;

  while (index < text.length) {
    const char = text[index];
    if (char === '#') {
      while (index < text.length && text[index] !== '\n') {
        out.comment(index);
        index += 1;
      }
      continue;
    }
    if (char === '"' || char === "'") {
      const prefix = quotePrefix(text, index);
      index = text.startsWith(char.repeat(3), index)
        ? blankTripleQuoted(text, index, out, char)
        : blankInterpolated(text, index, out, char, prefix);
      continue;
    }
    index += 1;
  }
  return out.finish();
}

/**
 * Whether the quote at @p index opens a C++ raw string.
 *
 * `R"delim(...)delim"` ends at its own delimiter, not at the next quote, and
 * the whole point of writing one is to hold characters that would otherwise
 * need escaping -- quotes and backslashes among them. Reading one as an
 * ordinary literal desynchronises the lexer for the rest of the line without
 * throwing, which is the failure a deny-by-default scan cannot afford: it would
 * report agreement over source it misread. The encoding prefixes are part of
 * the token, so `LR"` and `u8R"` open one too.
 */
function opensRawString(text: string, index: number): boolean {
  return /(?:^|[^\w])(?:L|u8|u|U)?R$/.test(text.slice(Math.max(0, index - 4), index));
}

/**
 * Blank a raw string and return the index after its closing quote.
 *
 * The delimiter is whatever stands between the quote and the first `(`, and the
 * literal ends at the first `)` followed by that same delimiter and a quote.
 */
function blankRawString(text: string, start: number, out: Blanker): number {
  const open = text.indexOf('(', start + 1);
  if (open === -1) {
    throw unterminated(text, start, 'raw string');
  }
  const closing = `)${text.slice(start + 1, open)}"`;
  const end = text.indexOf(closing, open + 1);
  if (end === -1) {
    throw unterminated(text, start, 'raw string');
  }
  for (let position = start + 1; position < end + closing.length - 1; position += 1) {
    out.literal(position);
  }
  return end + closing.length;
}

/**
 * Blank a single-line quoted run, keeping a formatted string's replacement
 * fields as code.
 *
 * What stands between the braces of an f-string is an expression the author
 * wrote, so a scan that blanked it would miss every read spelled there. A
 * doubled brace is the escape for a literal one and stays blanked.
 */
function blankInterpolated(
  text: string,
  start: number,
  out: Blanker,
  quote: string,
  prefix: string,
): number {
  if (!prefix.includes('f')) {
    return blankQuoted(text, start, out, quote, prefix.includes('r'));
  }
  const raw = prefix.includes('r');
  let index = start + 1;
  let braces = 0;
  while (index < text.length) {
    const char = text[index];
    if (char === '\n') {
      throw unterminated(text, start, 'string literal');
    }
    if (braces === 0) {
      if (char === '\\' && !raw) {
        out.literal(index);
        out.literal(index + 1);
        index += 2;
        continue;
      }
      if (char === quote) {
        return index + 1;
      }
      if (char === '{' && text[index + 1] !== '{') {
        braces = 1;
        index += 1;
        continue;
      }
      if ((char === '{' || char === '}') && text[index + 1] === char) {
        out.literal(index);
        out.literal(index + 1);
        index += 2;
        continue;
      }
      out.literal(index);
      index += 1;
      continue;
    }
    braces += char === '{' ? 1 : char === '}' ? -1 : 0;
    index += 1;
  }
  throw unterminated(text, start, 'string literal');
}

/** Blank a triple-quoted run, which is the one literal that spans lines. */
function blankTripleQuoted(text: string, start: number, out: Blanker, quote: string): number {
  const fence = quote.repeat(3);
  const end = text.indexOf(fence, start + 3);
  if (end === -1) {
    throw unterminated(text, start, 'triple-quoted string');
  }
  for (let position = start + 3; position < end; position += 1) {
    out.literal(position);
  }
  return end + 3;
}

/** Read a source file and lex it in the syntax its extension implies. */
export function lexFile(source: ProgramSource): LexedSource {
  return lexSource(readFileSync(source.absolutePath, 'utf8'), source.family);
}
