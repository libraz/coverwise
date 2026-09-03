/// Packaging contract for the published npm tarball.
///
/// `files` ships only `dist`, so the TypeScript sources a source map or a
/// declaration map would point at are never part of the tarball. Emitting either
/// kind of map therefore produces dangling references that warn in consumer
/// builds — the package emits none.

import { execFileSync } from 'node:child_process';
import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

let packedFileList: string[] | null = null;

/** Packing walks the whole tree; the default per-test window is too short. */
const packTimeoutMs = 60_000;

/**
 * File list of the tarball npm would publish.
 *
 * npm force-includes anything matching `readme{,.*}` in the package root, so a
 * second readme with a dotted suffix ships whatever `files` and `.npmignore`
 * say. Listing the tarball is the only way to see that. Packing costs seconds,
 * so the list is built once for the whole file.
 */
function packedFiles(): string[] {
  if (packedFileList === null) {
    const output = execFileSync(
      'npm',
      ['pack', '--dry-run', '--json', '--ignore-scripts', '--silent'],
      { cwd: repoRoot, encoding: 'utf8', maxBuffer: 16 * 1024 * 1024 },
    );
    const [tarball] = JSON.parse(output) as [{ files: Array<{ path: string }> }];
    packedFileList = tarball.files.map((file) => file.path);
  }
  return packedFileList;
}

function readJson(relativePath: string): Record<string, unknown> {
  return JSON.parse(readFileSync(path.join(repoRoot, relativePath), 'utf8'));
}

function collectFiles(dir: string): string[] {
  const files: string[] = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      files.push(...collectFiles(full));
    } else {
      files.push(full);
    }
  }
  return files;
}

describe('npm package contents', () => {
  it('ships only dist, which holds no TypeScript sources', () => {
    const pkg = readJson('package.json');
    expect(pkg.files).toEqual(['dist']);
  });

  it('emits neither source maps nor declaration maps', () => {
    const tsconfig = readJson('tsconfig.json');
    const options = tsconfig.compilerOptions as Record<string, unknown>;
    expect(options.sourceMap).toBe(false);
    expect(options.declarationMap).toBe(false);
  });

  it('has no map file in a built dist tree', () => {
    const dist = path.join(repoRoot, 'dist');
    if (!existsSync(dist)) {
      return;
    }
    const maps = collectFiles(dist)
      .filter((file) => file.endsWith('.map'))
      .map((file) => path.relative(repoRoot, file));
    expect(maps).toEqual([]);
  });

  it(
    'ships one readme, the one prepack fills with the npm text',
    () => {
      const readmes = packedFiles().filter((file) => /(^|\/)readme/i.test(file));
      expect(readmes).toEqual(['README.md']);

      // The one readme that ships carries the npm text, which prepack copies
      // over README.md from a name npm does not force-include.
      const pkg = readJson('package.json');
      const scripts = pkg.scripts as Record<string, string>;
      const source = scripts.prepack.match(/cp (\S+) README\.md/)?.[1];
      expect(source, 'prepack copies no file over README.md').toBeDefined();
      expect(/(^|\/)readme/i.test(source as string)).toBe(false);
      expect(existsSync(path.join(repoRoot, source as string))).toBe(true);
    },
    packTimeoutMs,
  );

  it(
    'actually excludes every path .npmignore declares excluded',
    () => {
      const ignored = readFileSync(path.join(repoRoot, '.npmignore'), 'utf8')
        .split('\n')
        .map((line) => line.trim())
        .filter((line) => line !== '' && !line.startsWith('#'));
      expect(ignored).not.toEqual([]);
      const packed = packedFiles();
      expect(packed.filter((file) => ignored.includes(file))).toEqual([]);
    },
    packTimeoutMs,
  );
});

describe('documented browser entry', () => {
  /// The WASM loader picks its Node branch off `globalThis.process`, so a CDN
  /// that rewrites imports through a Node compatibility layer makes browser
  /// loading fail inside the loader, far from the URL that caused it. The
  /// documented URL therefore has to name a file the package actually publishes
  /// on a CDN that serves it byte for byte.
  const verbatimHosts = ['cdn.jsdelivr.net', 'unpkg.com'];

  it('names a published file on a CDN that rewrites nothing', () => {
    const readme = readFileSync(path.join(repoRoot, 'npm-readme.md'), 'utf8');
    const section = readme.split('### Browser (CDN)')[1];
    expect(section, 'npm-readme.md has no browser section').toBeDefined();

    const match = section.match(/from '(https:\/\/[^']+)'/);
    expect(match, 'the browser snippet imports from no URL').not.toBeNull();
    const url = new URL((match as RegExpMatchArray)[1]);

    expect(verbatimHosts).toContain(url.hostname);
    expect(url.search).toBe('');
    expect(url.pathname.endsWith('/+esm')).toBe(false);

    const pkg = readJson('package.json');
    expect(url.pathname.endsWith(`/${pkg.main}`)).toBe(true);
  });
});

describe('npm entry points', () => {
  /// `exports` is invisible to the node10 resolution TypeScript still uses under
  /// `moduleResolution: 'node'`. Every documented subpath needs a `typesVersions`
  /// redirect for as long as the package keeps a top-level `types` field.
  it('resolves every documented subpath under node10 type resolution', () => {
    const pkg = readJson('package.json');
    const exports = pkg.exports as Record<string, unknown>;
    const typesVersions = pkg.typesVersions as Record<string, Record<string, string[]>>;

    expect(pkg.types).toBeTypeOf('string');
    const subpaths = Object.keys(exports).filter(
      (key) => key.startsWith('./') && key !== './package.json' && key !== './wasm',
    );
    expect(subpaths).not.toEqual([]);

    for (const subpath of subpaths) {
      const entry = exports[subpath] as Record<string, string>;
      const redirect = typesVersions['*'][subpath.slice(2)];
      expect(redirect, `no node10 type redirect for ${subpath}`).toBeDefined();
      expect(redirect).toEqual([entry.types.replace(/^\.\//, '')]);
    }
  });
});
