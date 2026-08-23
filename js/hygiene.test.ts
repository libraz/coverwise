/// Source hygiene guard for the published TypeScript surface.
///
/// Internal tracking identifiers — issue and finding labels written as a short
/// uppercase tag, a hyphen and a number — must never reach a public artifact:
/// they ship in the GitHub repository and print in `yarn test` output, where
/// they mean nothing to a consumer.

import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

/** Directories whose TypeScript sources are published or publicly readable. */
const scannedDirs = ['js', 'src/ts', 'tests/type', 'tests/wasm'];

/**
 * A short uppercase tag joined to a number by a hyphen, as a standalone token.
 * Word boundaries keep compound words such as `UTF-8` from matching.
 */
const trackingIdPattern = /\b[A-Z]{1,2}-\d{1,3}\b/;

function collectTypeScriptFiles(dir: string): string[] {
  const files: string[] = [];
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) {
      files.push(...collectTypeScriptFiles(full));
    } else if (entry.name.endsWith('.ts')) {
      files.push(full);
    }
  }
  return files;
}

describe('source hygiene', () => {
  it('carries no internal tracking identifiers in any public TypeScript source', () => {
    const offenders: string[] = [];
    for (const dir of scannedDirs) {
      for (const file of collectTypeScriptFiles(path.join(repoRoot, dir))) {
        const lines = readFileSync(file, 'utf8').split('\n');
        lines.forEach((line, index) => {
          const match = line.match(trackingIdPattern);
          if (match) {
            offenders.push(`${path.relative(repoRoot, file)}:${index + 1}: ${match[0]}`);
          }
        });
      }
    }
    expect(offenders).toEqual([]);
  });
});
