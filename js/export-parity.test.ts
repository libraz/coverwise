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
/// that the three sets agree.

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

const sorted = (names: Iterable<string>): string[] => [...names].sort();

const relativeToRoot = (path: string): string =>
  path.slice(decodeURIComponent(repoRoot.pathname).length);

const ENTRY_POINTS = [
  { name: 'js/index.ts', url: new URL('js/index.ts', repoRoot) },
  { name: 'js/pure/index.ts', url: new URL('js/pure/index.ts', repoRoot) },
];

describe('published type surface', () => {
  const entries = ENTRY_POINTS.map((entry) => ({
    ...entry,
    reExports: reExportsOf(entry.url),
  }));

  it('reads both entry points', () => {
    // Guards the enumeration itself: a rename that leaves this file pointing at
    // nothing would otherwise make every assertion below vacuously true.
    for (const entry of entries) {
      expect(entry.reExports.size, entry.name).toBeGreaterThan(0);
    }
  });

  it('re-exports from the same vocabulary modules', () => {
    const [wasm, pure] = entries;
    const relative = (paths: Iterable<string>): string[] => sorted(paths).map(relativeToRoot);
    expect(relative(pure.reExports.keys())).toEqual(relative(wasm.reExports.keys()));
  });

  it('re-exports every type its vocabulary modules export', () => {
    // Every entry point is reported in one run: the defect this guards is one
    // entry point falling behind the other, and an assertion that stops at the
    // first offender would name only one side of it.
    const gaps: Array<{ entryPoint: string; module: string; missing: string[]; extra: string[] }> =
      [];
    for (const entry of entries) {
      for (const [modulePath, reExport] of entry.reExports) {
        const declared = exportsOf(new URL(`file://${modulePath}`)).types;
        const reExported = reExport.star ? declared : reExport.localTypes;
        const missing = sorted([...declared].filter((name) => !reExported.has(name)));
        const extra = sorted([...reExported].filter((name) => !declared.has(name)));
        if (missing.length > 0 || extra.length > 0) {
          gaps.push({
            entryPoint: entry.name,
            module: relativeToRoot(modulePath),
            missing,
            extra,
          });
        }
      }
    }
    expect(gaps).toEqual([]);
  });

  it('publishes the same type names from both entry points', () => {
    const publicTypes = (entry: (typeof entries)[number]): string[] =>
      sorted([...entry.reExports.values()].flatMap((reExport) => [...reExport.types]));
    const [wasm, pure] = entries;
    expect(publicTypes(pure)).toEqual(publicTypes(wasm));
  });

  it('publishes the same re-exported value names from both entry points', () => {
    const publicValues = (entry: (typeof entries)[number]): string[] =>
      sorted([...entry.reExports.values()].flatMap((reExport) => [...reExport.values]));
    const [wasm, pure] = entries;
    expect(publicValues(pure)).toEqual(publicValues(wasm));
  });
});
