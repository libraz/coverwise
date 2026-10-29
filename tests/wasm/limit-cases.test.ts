import { readFileSync } from 'node:fs';
import path from 'node:path';
import { beforeAll, describe, expect, it } from 'vitest';
import * as wasm from '../../js/index.js';
import { CoverwiseError } from '../../js/index.js';
import * as pure from '../../js/pure/index.js';
import type { Parameter, TestCase } from '../../js/types.js';

/// The limit-case table is shared with the CLI and Python runners: each case
/// names a limit, how to build an input one step past it, and the message a
/// surface gives. This runner holds both TypeScript packages to it.

interface LimitCase {
  name: string;
  entry: 'generate' | 'analyze';
  build: {
    kind: string;
    count?: number;
    bytes?: number;
    parameters?: number;
    values?: number;
    strength?: number;
  };
  js: string;
  jsCode?: string;
}

const table = JSON.parse(
  readFileSync(path.resolve(import.meta.dirname, '../fixtures/limit_cases.json'), 'utf-8'),
) as { cases: LimitCase[] };

interface BuiltInput {
  parameters: Parameter[];
  constraints?: string[];
  strength?: number;
  tests?: TestCase[];
}

const named = (count: number, values: (i: number) => string[]): Parameter[] =>
  Array.from({ length: count }, (_, i) => ({ name: `p${i}`, values: values(i) }));

/// Builds the input a case describes, one step past its limit.
function build({ build: spec }: LimitCase): BuiltInput {
  const count = spec.count ?? 0;
  const bytes = spec.bytes ?? 0;
  switch (spec.kind) {
    case 'parameters':
      return { parameters: named(count, () => ['v']) };
    case 'values':
      return {
        parameters: [
          { name: 'p0', values: Array.from({ length: count }, (_, j) => `v${j}`) },
          { name: 'p1', values: ['a'] },
        ],
      };
    case 'rows':
      return {
        parameters: [
          { name: 'p0', values: ['v0'] },
          { name: 'p1', values: ['a'] },
        ],
        tests: Array.from({ length: count }, () => ({ p0: 'v0', p1: 'a' })),
      };
    case 'constraints':
      return {
        parameters: [
          { name: 'p0', values: ['a', 'b'] },
          { name: 'p1', values: ['x', 'y'] },
        ],
        constraints: Array.from({ length: count }, () => 'p0 = a'),
      };
    case 'long-value':
      return {
        parameters: [
          { name: 'p0', values: ['a'.repeat(bytes)] },
          { name: 'p1', values: ['x'] },
        ],
      };
    case 'long-values':
      return {
        parameters: [
          {
            name: 'p0',
            values: Array.from({ length: count }, (_, j) => `${j}:`.padEnd(bytes, 'a')),
          },
          { name: 'p1', values: ['x'] },
        ],
      };
    case 'uniform':
      return {
        parameters: named(spec.parameters ?? 0, () =>
          Array.from({ length: spec.values ?? 0 }, (_, j) => `v${j}`),
        ),
        strength: spec.strength,
      };
    default:
      throw new Error(`unknown builder ${spec.kind}`);
  }
}

const packages = [
  { name: 'npm', api: wasm },
  { name: 'pure', api: pure },
] as const;

beforeAll(async () => {
  await wasm.init();
});

describe('each documented input limit, one step past it', () => {
  for (const limit of table.cases) {
    for (const { name: surface, api } of packages) {
      it(`${limit.name} is refused on ${surface} with the documented message`, () => {
        const input = build(limit);
        let thrown: unknown;
        try {
          if (limit.entry === 'analyze') {
            api.analyzeCoverage(input.parameters, input.tests ?? [], input.strength);
          } else {
            api.generate(input);
          }
        } catch (error) {
          thrown = error;
        }
        expect(thrown).toBeInstanceOf(CoverwiseError);
        const refusal = thrown as CoverwiseError;
        expect(refusal.code).toBe(limit.jsCode ?? 'INVALID_INPUT');
        expect(refusal.message).toContain(limit.js);
      });
    }
  }
});
