import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';
import {
  discoverSources,
  isTestPath,
  type LexedSource,
  type LexicalFamily,
  lexSource,
  type ProgramSource,
  REPO_ROOT,
} from '../../../tests/util/source-scan.js';

/**
 * A structured error carries its message and its detail as two separate public
 * fields, so joining them into the text a user reads is something any file in
 * the tree can write. Written twice, the two spellings drift: one call site
 * leaves a separator behind when the detail is empty, another drops the detail
 * entirely, and the same failure reads differently depending on which surface
 * reported it. Taking a rendered line back apart is the same defect inverted --
 * a surface that recovers a half by stripping a prefix has restated the
 * rendering, backwards.
 *
 * This is the mechanism that keeps that composition single. It walks every
 * program source the repository holds, in whatever language it is written, and
 * fails on every read of an error's message or detail outside the two
 * renderings -- `SurfaceError` in C++ and `surfaceErrorText` in TypeScript. The
 * rule is deny-by-default: a read is an offence unless it matches one of the
 * shapes below, which are shapes rather than a list of forgiven files, because
 * such a list has to be extended by hand and is therefore only as good as the
 * memory of whoever writes the next call site.
 *
 * What the scan reaches is decided the same way. The reach is not a list of
 * directories or of extensions -- a binding written in a language nobody
 * anticipated would be outside such a list without anyone noticing, and the
 * mechanism would go on reporting agreement across a surface it cannot see.
 *
 * The scan reads the source tree rather than a compiled symbol because the
 * constraint is about where display text may be built, and neither language
 * expresses that in its type system while the fields are public.
 */

/** The one rendering per language, and the only place a join may live. */
const TEXT_MAPPINGS = ['src/model/surface_error.h', 'src/ts/model/error.ts'];

/**
 * A read of either half of an error, written as a member access.
 *
 * The lookahead lets an assignment through: `err.message = "..."` builds an
 * error rather than rendering one. A comparison is a read and stays caught.
 */
const READ_MOTIF = /\.(?:message|detail)\b(?!\s*=[^=])/;

/**
 * A read spelled as a subscript or looked up by name.
 *
 * `err['message']` reaches the same field as `err.message`, and a scan that
 * knew only the member access would be evaded by a notation an author picks
 * for reasons that have nothing to do with this rule. Matched against the view
 * that keeps literals, since the field name is spelled inside one.
 */
const SUBSCRIPT_READ_MOTIF =
  /\[\s*(['"])(?:message|detail)\1\s*\]|\b(?:getattr|hasattr)\s*\(\s*[^,)]+,\s*(['"])(?:message|detail)\2/;

/**
 * A read spelled as a binding pattern.
 *
 * `const { message, detail } = err` binds both halves without writing either
 * access, and what follows composes them out of plain identifiers that no
 * field-access motif can recognise. Catching the binding is what keeps the rest
 * of the function in scope of the rule.
 */
const DESTRUCTURED_READ_MOTIF =
  /\b(?:const|let|var|auto)\s+[{[][^}\]]*\b(?:message|detail)\b[^}\]]*[}\]]\s*=[^=]/;

/**
 * Recovering a part of a diagnostic and discarding the rest.
 *
 * The command-line surface writes the rendered text to its standard error, and
 * anything downstream receives it whole. Cutting a piece out of it -- the first
 * line, the text before a separator, one member of a partition -- states the
 * rendering's own shape a second time, in reverse and in another language, so a
 * reword on the rendering side silently stops matching and the caller is left
 * with a fragment. A constraint diagnostic quotes the expression before giving
 * the reason, and expressions may span lines, so taking the first line alone
 * hands back the caller's own input with the explanation removed.
 *
 * Taking a wrapper off and passing the remainder on is not this. The stream
 * envelope a writer adds is framing rather than part of what was rendered, and
 * removing it loses nothing: what comes back is the whole text with a fixed
 * affix gone. Hence the two shapes below turn on discarding -- a split that is
 * then subscripted, a partition, a slice given both an origin and an end. A
 * slice given only an origin, like a prefix removal, keeps the remainder.
 */
const DECOMPOSITION_MOTIF =
  /\b(?:stderr|stdout|output|diagnostic)\b[\w.[\]()]*\.(?:(?:split|splitlines)\s*\([^)]*\)\s*\[|(?:partition|rpartition)\s*\(|(?:slice|substring|substr)\s*\([^)]*,)/;

/** Two halves joined into one string, in any of the languages' spellings. */
const JOIN_MOTIF = /[+`%]|\$\{|\.(?:join|format)\s*\(|\{[^}]*\}/;

/** Both halves read on one line, with whatever stands between them. */
const PAIRED_READS = /\.(?:message|detail)\b(.*?)\.(?:message|detail)\b/;

/**
 * A join written one call out from the two reads rather than between them.
 *
 * `[error.message, error.detail].join(': ')` and `'%s: %s' % (message, detail)`
 * put nothing between the two reads but a comma, so they look exactly like
 * handing both halves on unjoined -- and they compose the text anyway, one call
 * further out. Whichever side the call sits on, it disqualifies the hand-off.
 */
const HELPER_JOIN_MOTIF = /\.(?:join|format)\s*\(|['"`]\s*%/;

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
 * Reading the message off a value the engine did not raise.
 *
 * A foreign throw is not one of the engine's structured errors: it has no
 * detail to lose and no rendering to go through. What establishes that it is
 * foreign has to stand in the same expression as the read -- either a type test
 * against the builtin, or a static type that does not promise the field is
 * there at all -- so this cannot cover a structured error that merely happens
 * to sit near a guard. Recognising the property rather than one spelling of it
 * is deliberate: an allowance tied to a single notation makes the rule around
 * it notation-dependent in exactly the way the rule itself may not be.
 */
function isForeignThrowRead(code: string): boolean {
  const foreign = /instanceof Error\b|message\?\s*:\s*unknown/.test(code);
  return foreign && !/\.detail\b/.test(code);
}

/**
 * Handing both halves across a surface boundary as separate values.
 *
 * The receiving side still holds two fields and still decides how to render
 * them, so no display text is built here. The two reads have to be separate
 * arguments: anything joining them is the composition this check exists to
 * catch, so a `+`, a template placeholder, a backtick or a formatting call
 * between them disqualifies the hand-off -- and so does one that collects them
 * first and joins the collection, which puts the composition outside the two
 * reads while leaving a bare comma between them.
 */
function isUnjoinedHandoff(statement: string): boolean {
  const pair = PAIRED_READS.exec(statement);
  if (pair === null) {
    return false;
  }
  const between = pair[1];
  return between.includes(',') && !JOIN_MOTIF.test(between) && !HELPER_JOIN_MOTIF.test(statement);
}

/**
 * The statement each line belongs to.
 *
 * An argument list the formatter broke across lines is one expression, and
 * judging its lines separately would read a hand-off that spans four lines as
 * four lone reads. Statements end at `;` or at a brace, which is as much syntax
 * as this needs to know. A language without statement terminators ends its
 * statements at the newline, which the same rule gives for free once the last
 * line of the file closes the run.
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

/** Where a source composes or decomposes the text of an error. */
interface Offence {
  relativePath: string;
  line: number;
  text: string;
}

/**
 * Every offence in one source, given the two views the lexer produced.
 *
 * Taken as text rather than as a path so the same reading runs against a source
 * held in memory, which is how the mechanism is shown to report a violation it
 * is deliberately given.
 */
function offencesIn(relativePath: string, lexed: LexedSource): Offence[] {
  const raw = lexed.code.split('\n');
  const literalLines = lexed.withoutComments.split('\n');
  const statements = statementTexts(raw);
  const offences: Offence[] = [];

  for (let index = 0; index < raw.length; index += 1) {
    const code = raw[index];
    const withLiterals = literalLines[index];
    const read =
      READ_MOTIF.test(code) ||
      SUBSCRIPT_READ_MOTIF.test(withLiterals) ||
      DESTRUCTURED_READ_MOTIF.test(code);
    const decomposes = DECOMPOSITION_MOTIF.test(withLiterals);
    if (!read && !decomposes) {
      continue;
    }
    if (read && !decomposes) {
      if (isMessageAnnotation(code) || isForeignThrowRead(code)) {
        continue;
      }
      if (isUnjoinedHandoff(statements[index])) {
        continue;
      }
    }
    // Reported from the view that kept its literals: an offence names the line
    // the author would go and read, not the blanks the lexer left behind.
    offences.push({ relativePath, line: index + 1, text: withLiterals.trim() });
  }
  return offences;
}

/** Read a source in the syntax its extension implies and scan it. */
function scanFile(relativePath: string, absolutePath: string, family: LexicalFamily): Offence[] {
  try {
    return offencesIn(relativePath, lexSource(readFileSync(absolutePath, 'utf8'), family));
  } catch (cause) {
    // A source the lexer cannot read is a source the scan did not look at, so
    // it has to name itself rather than surface as a failure with no location.
    throw new Error(`${relativePath}: ${(cause as Error).message}`, { cause });
  }
}

describe('an error becomes text in one place', () => {
  const discovered = discoverSources();
  const sources = discovered.programs.filter(
    (source) =>
      !isTestPath(source.relativePath) &&
      !TEXT_MAPPINGS.some((mapping) => source.relativePath === path.normalize(mapping)),
  );

  it('classifies every file the repository ships', () => {
    // An extension in neither the program languages nor the data formats is a
    // language the scan does not know how to read. Reporting it here is what
    // turns "a binding was added in a new language" into a failure rather than
    // into silence. Test trees are exempt, and hold the sample that shows this
    // reporting works.
    expect(discovered.unclassified.filter((file) => !isTestPath(file))).toEqual([]);
  });

  it('reaches every language the engine is written in', () => {
    // Guards the discovery itself: a walk that found nothing, or that lost a
    // language, would make the scan below vacuously clean.
    expect(sources.length).toBeGreaterThan(10);
    const families = new Set(sources.map((source) => source.family));
    expect([...families].sort()).toEqual(['c-like', 'hash-like', 'js-like']);
    const extensions = new Set(sources.map((source) => source.extension));
    for (const extension of ['cpp', 'h', 'ts', 'py']) {
      expect({ [extension]: extensions.has(extension) }).toEqual({ [extension]: true });
    }
  });

  it('reaches the surfaces outside the C++ and TypeScript engine', () => {
    // The engine's own directories are the ones a scan is written against, and
    // the ones a hand-kept reach would have listed. A binding that wraps the
    // executable from another directory is exactly what such a reach loses.
    const roots = new Set(sources.map((source) => source.relativePath.split(path.sep)[0]));
    for (const root of ['src', 'tools', 'js', 'bindings']) {
      expect({ [root]: roots.has(root) }).toEqual({ [root]: true });
    }
  });

  it('finds no source composing or taking apart the text of an error', () => {
    const offences = sources.flatMap((source) =>
      scanFile(source.relativePath, source.absolutePath, source.family),
    );
    expect(
      offences.map((offence) => `${offence.relativePath}:${offence.line}: ${offence.text}`),
    ).toEqual([]);
  });

  it('finds the join inside each rendering', () => {
    for (const mapping of TEXT_MAPPINGS) {
      const normalized = path.normalize(mapping);
      // Located through the same walk as everything else, so a rendering that
      // moved is a missing file here rather than an allowance pointing at
      // nothing while the scan reports the tree clean.
      const rendering = discovered.programs.find(
        (source) => source.relativePath === normalized,
      ) as ProgramSource;
      expect({ [mapping]: rendering !== undefined }).toEqual({ [mapping]: true });
      const joined = lexSource(readFileSync(rendering.absolutePath, 'utf8'), rendering.family)
        .code.split('\n')
        .some(
          (code) => /\.message\b/.test(code) && /\.detail\b/.test(code) && JOIN_MOTIF.test(code),
        );
      expect({ [mapping]: joined }).toEqual({ [mapping]: true });
    }
  });
});

/**
 * Sources written to break the rule, and the lines each has to be reported at.
 *
 * A scan that has only ever been run against sources that obey it is a scan
 * nobody has watched work. Every notation below reaches the same field as the
 * plain member access, and each is one an author picks for reasons that have
 * nothing to do with this rule -- which is what makes the absence of any of
 * them a way through rather than a hypothetical.
 */
const VIOLATIONS: ReadonlyArray<{
  notation: string;
  family: LexicalFamily;
  source: string;
  offending: number[];
}> = [
  {
    notation: 'a member access interpolated into a template',
    family: 'js-like',
    source: [
      'function render(error: ErrorInfo): string {',
      // biome-ignore lint/suspicious/noTemplateCurlyInString: source text handed to the scan, not text to interpolate here
      '  return `${error.message}: ${error.detail}`;',
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a binding pattern that takes both halves apart',
    family: 'js-like',
    source: [
      'function render(error: ErrorInfo): string {',
      '  const { message, detail } = error;',
      '  return detail ? message + ": " + detail : message;',
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a subscript naming the field in a string',
    family: 'js-like',
    source: [
      'function render(error: ErrorInfo): string {',
      '  return error["message"] + ": " + error["detail"];',
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a comment marker written inside a string literal',
    family: 'js-like',
    source: [
      'function render(error: ErrorInfo): string {',
      '  return "see https://example.invalid -- " + error.message + ": " + error.detail;',
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a comment marker written inside a C++ string literal',
    family: 'c-like',
    source: [
      'std::string Render(const Error& error) {',
      '  return "see https://example.invalid -- " + error.message + ": " + error.detail;',
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'an attribute read in a formatted string',
    family: 'hash-like',
    source: ['def render(error):', '    return f"{error.message}: {error.detail}"'].join('\n'),
    offending: [2],
  },
  {
    notation: 'a field looked up by name',
    family: 'hash-like',
    source: [
      'def render(error):',
      '    return getattr(error, "message") + ": " + getattr(error, "detail")',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a join applied to a collection of the two halves',
    family: 'js-like',
    source: [
      'function render(error: ErrorInfo): string {',
      "  return [error.message, error.detail].join(': ');",
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a join written before the two halves it collects',
    family: 'hash-like',
    source: ['def render(error):', '    return ": ".join([error.message, error.detail])'].join(
      '\n',
    ),
    offending: [2],
  },
  {
    notation: 'a format string given both halves as arguments',
    family: 'hash-like',
    source: ['def render(error):', '    return "%s: %s" % (error.message, error.detail)'].join(
      '\n',
    ),
    offending: [2],
  },
  {
    notation: 'a join written after a raw string that holds quotes of its own',
    family: 'c-like',
    source: [
      'const char* kSample = R"({"name":"A","values":["a0"]})";',
      'std::string Render(const Error& error) {',
      '  return error.message + ": " + error.detail;',
      '}',
    ].join('\n'),
    offending: [3],
  },
  {
    notation: 'a diagnostic truncated to its first line',
    family: 'hash-like',
    source: ['def reason_of(outcome):', '    return outcome.stderr.splitlines()[0]'].join('\n'),
    offending: [2],
  },
  {
    notation: 'a diagnostic cut at its separator',
    family: 'hash-like',
    source: ['def reason_of(outcome):', '    return outcome.stderr.partition(": ")[2]'].join('\n'),
    offending: [2],
  },
  {
    notation: 'a diagnostic split and indexed, in TypeScript',
    family: 'js-like',
    source: [
      'function reasonOf(outcome: Outcome): string {',
      "  return outcome.stderr.split('\\n')[0];",
      '}',
    ].join('\n'),
    offending: [2],
  },
  {
    notation: 'a diagnostic sliced to a part, in C++',
    family: 'c-like',
    source: [
      'std::string ReasonOf(const std::string& diagnostic) {',
      '  return diagnostic.substr(0, diagnostic.find(": "));',
      '}',
    ].join('\n'),
    offending: [2],
  },
];

/**
 * Sources that use the same words without breaking the rule.
 *
 * Half of a deny-by-default scan's worth is that its allowances hold: one that
 * reported these would be turned off within a week, and a scan nobody runs
 * guards nothing.
 */
const NON_VIOLATIONS: ReadonlyArray<{ shape: string; family: LexicalFamily; source: string }> = [
  {
    shape: 'handing both halves across a boundary unjoined',
    family: 'js-like',
    source: 'report(error.message, error.detail);',
  },
  {
    shape: 'annotating an error with a message of its own',
    family: 'js-like',
    source: "error.message = 'Invalid input: the model declares no parameters';",
  },
  {
    shape: 'describing a value the engine did not raise',
    family: 'js-like',
    source: 'const text = (thrown as { message?: unknown })?.message;',
  },
  {
    shape: 'prose about the rule, in a comment',
    family: 'js-like',
    source: '// Joining error.message and error.detail here would be an offence.',
  },
  {
    shape: 'the rule quoted inside a string literal',
    family: 'js-like',
    source: 'const documented = \'error.message + ": " + error.detail\';',
  },
  {
    shape: 'prose about splitting a diagnostic, in a comment',
    family: 'hash-like',
    source: '# Calling stderr.split(": ")[0] here would restate the rendering.',
  },
  {
    shape: 'passing a diagnostic on whole',
    family: 'hash-like',
    source: 'raise CoverwiseError(code, message, outcome.returncode, outcome.stderr)',
  },
  {
    shape: 'taking the stream envelope off and keeping the rest',
    family: 'hash-like',
    source: 'message = outcome.stderr.removeprefix("error: ")',
  },
  {
    shape: 'taking the stream envelope off and keeping the rest, in TypeScript',
    family: 'js-like',
    source: "const message = outcome.stderr.replace(/^error: /, '');",
  },
  {
    shape: 'taking the stream envelope off by its length, in C++',
    family: 'c-like',
    source: 'std::string message = diagnostic.substr(kEnvelope.size());',
  },
  {
    shape: 'the rule quoted inside a raw string',
    family: 'c-like',
    source: 'const char* kDoc = R"(error.message + ": " + error.detail)";',
  },
  {
    shape: 'reading every line of a diagnostic rather than one of them',
    family: 'hash-like',
    source: 'for line in outcome.stderr.splitlines():',
  },
];

describe('the scan reports a source that breaks the rule', () => {
  for (const { notation, family, source, offending } of VIOLATIONS) {
    it(`reports ${notation}`, () => {
      const offences = offencesIn('fixture', lexSource(source, family));
      expect(offences.map((offence) => offence.line)).toEqual(offending);
    });
  }

  for (const { shape, family, source } of NON_VIOLATIONS) {
    it(`leaves ${shape} alone`, () => {
      expect(offencesIn('fixture', lexSource(source, family))).toEqual([]);
    });
  }

  it('refuses a source it cannot read rather than reading past the end of it', () => {
    // A lexer that ran off the end of an unterminated literal would treat the
    // rest of the file as text and report the tree clean over source it never
    // looked at, which is the one way this mechanism could fail silently.
    expect(() =>
      lexSource('const unterminated = "no closing quote\nreturn 1;\n', 'js-like'),
    ).toThrow(/Unterminated/);
  });
});

describe('the scan reaches a file nothing names', () => {
  const fixtureRoot = path.join(REPO_ROOT, 'tests', 'util', 'scan-fixtures');
  const discovered = discoverSources(fixtureRoot);

  it('finds a source nested below any directory that was ever listed', () => {
    const found = discovered.programs.map((source) => source.relativePath);
    expect(found).toContain(path.join('nested', 'deeper', 'renders-an-error.ts'));
  });

  it('reports a file in a language it has no reading for', () => {
    expect(discovered.unclassified).toEqual(['a-language-nobody-classified.zz']);
  });

  it('reports the offence in the source it reached that way', () => {
    const offences = discovered.programs.flatMap((source) =>
      scanFile(source.relativePath, source.absolutePath, source.family),
    );
    expect(offences.map((offence) => offence.text)).toEqual([
      "return [error.message, error.detail].join(': ');",
    ]);
  });
});
