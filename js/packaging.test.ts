/// Packaging contract for the published npm tarball.
///
/// `files` ships only `dist`, so the TypeScript sources a source map or a
/// declaration map would point at are never part of the tarball. Emitting either
/// kind of map therefore produces dangling references that warn in consumer
/// builds — the package emits none.

import { existsSync, readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

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
});
