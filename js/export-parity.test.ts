/// The published type surface, enumerated from the sources rather than listed.
///
/// `@libraz/coverwise` and `@libraz/coverwise/pure` are advertised as the same
/// API, and a consumer who can name a member of a documented return shape from
/// one of them must be able to name it from the other. Assigning one entry
/// point's module type to the other covers everything that exists at run time,
/// but a type-only export leaves no trace in a module's value type, so that
/// check cannot see the exported types at all.
///
/// What filled the gap was a list of names kept by hand, which is a mechanism
/// only as long as everyone remembers to extend it: a type added to the
/// vocabulary and re-exported from one entry point produced no error anywhere.
/// So this reads the export declarations themselves. The names are never
/// written down here — they are collected from the files, and the assertion is
/// that the sets agree.
///
/// Agreeing on names is weaker than agreeing on shapes, and the difference is
/// the whole point of the surface: two entry points can both publish
/// `GenerateResult` and mean different things by it. What closes that gap
/// without a list of type-level assertions to maintain is where the names come
/// from. A published type is required to be a re-export — never declared in an
/// entry point — and each name is required to come from the same vocabulary
/// module on both sides. Two names that resolve to one declaration in one file
/// cannot differ in shape, so identity of source is identity of shape, and it
/// is checked by enumeration like everything else here.

import ts from 'typescript';
import { describe, expect, it } from 'vitest';

/// A module's exported names, split the way the module system does.
interface ExportedNames {
  /// Names exported as types (`export interface`, `export type`, and the
  /// `export type { ... }` form).
  types: Set<string>;
  /// Names exported as values. Their parity is already enforced structurally by
  /// the module-type assignment in the type tier; they are collected here to
  /// keep the two halves of the surface visible in one place.
  values: Set<string>;
}

/// What one entry point re-exports from one vocabulary module.
interface ReExport extends ExportedNames {
  /// Local names behind any renaming, which is what the vocabulary module has
  /// to be able to supply.
  localTypes: Set<string>;
  /// `export * from`, which re-exports whatever the module exports and so needs
  /// no enumeration of its own.
  star: boolean;
}

/// One entry point, read from its source.
interface Surface {
  name: string;
  /// What it declares itself rather than re-exporting. A type here is published
  /// from a declaration only this entry point has.
  own: ExportedNames;
  /// What it re-exports, keyed by the module it re-exports from.
  reExports: Map<string, ReExport>;
}

const repoRoot = new URL('../', import.meta.url);

function read(url: URL): string {
  const path = decodeURIComponent(url.pathname);
  const text = ts.sys.readFile(path);
  if (text === undefined) {
    throw new Error(`Cannot read ${path}`);
  }
  return text;
}

function parse(url: URL): ts.SourceFile {
  return ts.createSourceFile(
    decodeURIComponent(url.pathname),
    read(url),
    ts.ScriptTarget.Latest,
    true,
    ts.ScriptKind.TS,
  );
}

function isExported(node: ts.Node): boolean {
  return (
    ts.canHaveModifiers(node) &&
    (ts.getModifiers(node) ?? []).some((modifier) => modifier.kind === ts.SyntaxKind.ExportKeyword)
  );
}

/// A module specifier as written resolves to a sibling source file: the package
/// is authored with the extension the runtime sees, so `./types.js` on disk is
/// `types.ts`.
function resolveSpecifier(from: URL, specifier: string): URL {
  return new URL(specifier.replace(/\.js$/, '.ts'), from);
}

/// Everything a module exports under its own name.
function exportsOf(url: URL): ExportedNames {
  const source = parse(url);
  const types = new Set<string>();
  const values = new Set<string>();

  for (const statement of source.statements) {
    if (ts.isInterfaceDeclaration(statement) || ts.isTypeAliasDeclaration(statement)) {
      if (isExported(statement)) {
        types.add(statement.name.text);
      }
      continue;
    }
    if (ts.isClassDeclaration(statement) || ts.isFunctionDeclaration(statement)) {
      if (isExported(statement) && statement.name) {
        values.add(statement.name.text);
      }
      continue;
    }
    if (ts.isEnumDeclaration(statement)) {
      if (isExported(statement)) {
        values.add(statement.name.text);
      }
      continue;
    }
    if (ts.isVariableStatement(statement) && isExported(statement)) {
      for (const declaration of statement.declarationList.declarations) {
        if (ts.isIdentifier(declaration.name)) {
          values.add(declaration.name.text);
        }
      }
      continue;
    }
    // `export { A, B }` / `export type { C }` without a module specifier.
    if (
      ts.isExportDeclaration(statement) &&
      !statement.moduleSpecifier &&
      statement.exportClause &&
      ts.isNamedExports(statement.exportClause)
    ) {
      for (const element of statement.exportClause.elements) {
        const target = statement.isTypeOnly || element.isTypeOnly ? types : values;
        target.add(element.name.text);
      }
    }
  }

  return { types, values };
}

/// What an entry point re-exports, keyed by the module it re-exports from.
function reExportsOf(url: URL): Map<string, ReExport> {
  const source = parse(url);
  const byModule = new Map<string, ReExport>();

  for (const statement of source.statements) {
    if (!ts.isExportDeclaration(statement) || !statement.moduleSpecifier) {
      continue;
    }
    if (!ts.isStringLiteral(statement.moduleSpecifier)) {
      continue;
    }

    const target = decodeURIComponent(
      resolveSpecifier(url, statement.moduleSpecifier.text).pathname,
    );
    let entry = byModule.get(target);
    if (!entry) {
      entry = {
        types: new Set(),
        values: new Set(),
        localTypes: new Set(),
        star: false,
      };
      byModule.set(target, entry);
    }

    if (!statement.exportClause) {
      entry.star = true;
      continue;
    }
    if (ts.isNamespaceExport(statement.exportClause)) {
      entry.values.add(statement.exportClause.name.text);
      continue;
    }
    for (const element of statement.exportClause.elements) {
      if (statement.isTypeOnly || element.isTypeOnly) {
        entry.types.add(element.name.text);
        entry.localTypes.add((element.propertyName ?? element.name).text);
      } else {
        entry.values.add(element.name.text);
      }
    }
  }

  return byModule;
}

function surfaceOf(name: string, url: URL): Surface {
  return { name, own: exportsOf(url), reExports: reExportsOf(url) };
}

const sorted = (names: Iterable<string>): string[] => [...names].sort();

const relativeToRoot = (path: string): string =>
  path.slice(decodeURIComponent(repoRoot.pathname).length);

/// A vocabulary module an entry point does not re-export in full.
interface Gap {
  entryPoint: string;
  module: string;
  missing: string[];
  extra: string[];
}

/// Where an entry point re-exports less, or more, than a module declares.
function gapsAgainstVocabulary(surface: Surface): Gap[] {
  const gaps: Gap[] = [];
  for (const [modulePath, reExport] of surface.reExports) {
    const declared = exportsOf(new URL(`file://${modulePath}`)).types;
    const reExported = reExport.star ? declared : reExport.localTypes;
    const missing = sorted([...declared].filter((name) => !reExported.has(name)));
    const extra = sorted([...reExported].filter((name) => !declared.has(name)));
    if (missing.length > 0 || extra.length > 0) {
      gaps.push({ entryPoint: surface.name, module: relativeToRoot(modulePath), missing, extra });
    }
  }
  return gaps;
}

/// Every published type name, with the module its declaration lives in.
///
/// A star re-export publishes whatever the module declares, so its names are
/// taken from the module. A renaming publishes the outer name and keeps the
/// module it came from, which is what makes two entry points that rename the
/// same declaration differently show up as a name difference rather than as a
/// source difference.
function typeSources(surface: Surface): Map<string, string> {
  const sources = new Map<string, string>();
  for (const [modulePath, reExport] of surface.reExports) {
    const published = reExport.star
      ? exportsOf(new URL(`file://${modulePath}`)).types
      : reExport.types;
    for (const name of published) {
      sources.set(name, relativeToRoot(modulePath));
    }
  }
  return sources;
}

/// Names published by both entry points out of different declarations.
///
/// Two entry points can agree on every name while one of them means a
/// different declaration by it, and a check that compared names alone would
/// call that parity. There is nothing to compare shapes with at this level, so
/// what is compared instead is where each name comes from: one declaration
/// cannot disagree with itself.
function divergentTypeSources(left: Surface, right: Surface): string[] {
  const leftSources = typeSources(left);
  const rightSources = typeSources(right);
  const divergent: string[] = [];
  for (const [name, modulePath] of leftSources) {
    const other = rightSources.get(name);
    if (other !== undefined && other !== modulePath) {
      divergent.push(`${name}: ${left.name} from ${modulePath}, ${right.name} from ${other}`);
    }
  }
  return divergent.sort();
}

const publishedTypes = (surface: Surface): string[] => sorted(typeSources(surface).keys());

const publishedValues = (surface: Surface): string[] =>
  sorted([...surface.reExports.values()].flatMap((reExport) => [...reExport.values]));

const ENTRY_POINTS = [
  { name: 'js/index.ts', url: new URL('js/index.ts', repoRoot) },
  { name: 'js/pure/index.ts', url: new URL('js/pure/index.ts', repoRoot) },
];

describe('published type surface', () => {
  const [wasm, pure] = ENTRY_POINTS.map((entry) => surfaceOf(entry.name, entry.url));

  it('reads both entry points', () => {
    // Guards the enumeration itself: a rename that leaves this file pointing at
    // nothing would otherwise make every assertion below vacuously true.
    for (const surface of [wasm, pure]) {
      expect(surface.reExports.size, surface.name).toBeGreaterThan(0);
      expect(publishedTypes(surface).length, surface.name).toBeGreaterThan(0);
    }
  });

  it('re-exports from the same vocabulary modules', () => {
    const relative = (paths: Iterable<string>): string[] => sorted(paths).map(relativeToRoot);
    expect(relative(pure.reExports.keys())).toEqual(relative(wasm.reExports.keys()));
  });

  it('re-exports every type its vocabulary modules export', () => {
    // Every entry point is reported in one run: the defect this guards is one
    // entry point falling behind the other, and an assertion that stops at the
    // first offender would name only one side of it.
    expect([...gapsAgainstVocabulary(wasm), ...gapsAgainstVocabulary(pure)]).toEqual([]);
  });

  it('publishes every type from a shared declaration rather than one of its own', () => {
    // This is what makes name parity mean shape parity. A type declared in an
    // entry point is published from a declaration the other entry point has no
    // access to, so the two can drift apart in shape while every name still
    // agrees -- and nothing at this level can compare the shapes. Requiring the
    // declaration to live in a vocabulary module keeps the question from
    // arising instead of answering it with a list of assertions per name.
    expect({ wasm: sorted(wasm.own.types), pure: sorted(pure.own.types) }).toEqual({
      wasm: [],
      pure: [],
    });
  });

  it('publishes each type name out of the same declaration on both entry points', () => {
    expect(divergentTypeSources(wasm, pure)).toEqual([]);
  });

  it('publishes the same type names from both entry points', () => {
    expect(publishedTypes(pure)).toEqual(publishedTypes(wasm));
  });

  it('publishes the same re-exported value names from both entry points', () => {
    expect(publishedValues(pure)).toEqual(publishedValues(wasm));
  });
});

/// Entry-point pairs written to diverge, one defect each.
///
/// A comparison that has only ever run over a surface in agreement is a
/// comparison nobody has watched report anything. Each pair below is a way two
/// entry points drift that a reader would call the same API, and the assertion
/// is that the comparison says otherwise.
const FIXTURES = new URL('tests/util/export-fixtures/', repoRoot);

function fixturePair(directory: string): [Surface, Surface] {
  const base = new URL(`${directory}/`, FIXTURES);
  return [
    surfaceOf('wasm-entry.ts', new URL('wasm-entry.ts', base)),
    surfaceOf('pure-entry.ts', new URL('pure-entry.ts', base)),
  ];
}

describe('the comparison reports two entry points that have drifted', () => {
  it('accepts a pair that agrees', () => {
    const [wasm, pure] = fixturePair('agreeing');
    expect(publishedTypes(wasm)).toEqual(publishedTypes(pure));
    expect(divergentTypeSources(wasm, pure)).toEqual([]);
    expect([...gapsAgainstVocabulary(wasm), ...gapsAgainstVocabulary(pure)]).toEqual([]);
    expect({ wasm: sorted(wasm.own.types), pure: sorted(pure.own.types) }).toEqual({
      wasm: [],
      pure: [],
    });
  });

  it('reports a type one entry point publishes and the other does not', () => {
    const [wasm, pure] = fixturePair('one-name-short');
    expect(publishedTypes(wasm)).not.toEqual(publishedTypes(pure));
  });

  it('reports a type a vocabulary module declares that neither entry point passes on', () => {
    const [wasm] = fixturePair('one-name-short');
    expect(gapsAgainstVocabulary(wasm)).toEqual([
      {
        entryPoint: 'wasm-entry.ts',
        module: 'tests/util/export-fixtures/one-name-short/vocabulary.ts',
        missing: ['Unpublished'],
        extra: [],
      },
    ]);
  });

  it('reports the same name published out of two different declarations', () => {
    // The case a name-level comparison calls parity: both entry points publish
    // `Report`, and the two declarations disagree about what it is.
    const [wasm, pure] = fixturePair('same-name-other-shape');
    expect(publishedTypes(wasm)).toEqual(publishedTypes(pure));
    expect(divergentTypeSources(wasm, pure)).toEqual([
      'Report: wasm-entry.ts from tests/util/export-fixtures/same-name-other-shape/wasm-vocabulary.ts, pure-entry.ts from tests/util/export-fixtures/same-name-other-shape/pure-vocabulary.ts',
    ]);
  });

  it('reports a type each entry point declares for itself', () => {
    // The same divergence written without a second module: each entry point
    // declares `Handle`, the two declarations disagree, and every name still
    // matches. Nothing that compares names, and nothing that assigns one module
    // type to the other, can see this.
    const [wasm, pure] = fixturePair('declared-in-place');
    expect(publishedTypes(wasm)).toEqual(publishedTypes(pure));
    expect(divergentTypeSources(wasm, pure)).toEqual([]);
    expect({ wasm: sorted(wasm.own.types), pure: sorted(pure.own.types) }).toEqual({
      wasm: ['Handle'],
      pure: ['Handle'],
    });
  });
});
