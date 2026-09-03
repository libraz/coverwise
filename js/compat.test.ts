/// Compatibility tests: WASM engine vs pure TypeScript engine.
///
/// Runs identical inputs through both engines and verifies they produce
/// equivalent results (coverage, stats, test counts, and exact output).

import { beforeAll, describe, expect, it } from 'vitest';
import { analyzeCoverage, Coverwise, estimateModel, extendTests, generate, init } from './index.js';
import {
  analyzeCoverage as pureAnalyzeCoverage,
  estimateModel as pureEstimateModel,
  extendTests as pureExtendTests,
  generate as pureGenerate,
} from './pure/index.js';
import type {
  ExtendInput,
  GenerateInput,
  GenerateResult,
  ModelStats,
  Parameter,
  TestCase,
} from './types.js';

// --- TS engine imports ---

import {
  estimateModel as tsEstimateModelRaw,
  extend as tsExtendRaw,
  generate as tsGenerateRaw,
} from '../src/ts/core/generator.js';
import type { GenerateOptions } from '../src/ts/model/generate-options.js';
import type { Parameter as InternalParameter } from '../src/ts/model/parameter.js';
import type {
  TestCase as InternalTestCase,
  UncoveredTuple as InternalUncoveredTuple,
} from '../src/ts/model/test-case.js';
import type { CoverageReport as InternalCoverageReport } from '../src/ts/validator/coverage-validator.js';
import { validateCoverage } from '../src/ts/validator/coverage-validator.js';

// --- Adapter imports (shared conversion logic) ---

import {
  toInternalOptions,
  toInternalParams,
  toInternalTestCase,
  toPublicTestCase,
} from './pure/adapter.js';

// --- Published vocabulary, enumerated by the compiler ---

import {
  GENERATE_INPUT_FIELDS,
  PARAMETER_FIELDS,
  PARAMETER_VALUE_FIELDS,
} from '../tests/type/public-vocabulary.js';

// ---------------------------------------------------------------------------
// Thin wrappers around adapter functions for compat-test convenience
// ---------------------------------------------------------------------------

/** Convert public TestCase to internal, delegating to adapter. */
function namedTestToInternal(namedTest: TestCase, params: InternalParameter[]): InternalTestCase {
  return toInternalTestCase(namedTest, params);
}

/// Convert an internal TestCase to its public form the way a shipped surface
/// does: a row's position in the suite is its alias rotation, so a value with
/// aliases is displayed under a different one from row to row. Rendering every
/// row at rotation 0 would compare the raw engine's output against a rule the
/// shipped surfaces do not use, and no model without aliases can tell the
/// difference.
function internalTestToNamed(
  tc: InternalTestCase,
  params: InternalParameter[],
  rotation = 0,
): TestCase {
  return toPublicTestCase(tc, params, rotation);
}

/** Build GenerateOptions from GenerateInput, delegating to adapter. */
function buildGenerateOptions(input: GenerateInput, params: InternalParameter[]): GenerateOptions {
  return toInternalOptions(input, params);
}

// ---------------------------------------------------------------------------
// TS engine wrappers (the raw engine, named the way the WASM API names things)
// ---------------------------------------------------------------------------

/// What the raw-engine wrappers below actually return.
///
/// Deliberately not the public `GenerateResult`. A shipped surface projects the
/// engine's output before handing it over: it renders `display` onto every
/// uncovered tuple and normalises `negativeTests` into an array. These wrappers
/// do neither, so declaring them as the public shape would state a projection
/// they do not perform -- and every comparison written against that declaration
/// would be reading fields the value does not carry. Declaring the engine shape
/// keeps the comparisons below honest about which side they are reading.
interface EngineResult {
  tests: TestCase[];
  negativeTests: TestCase[] | undefined;
  coverage: number;
  uncovered: InternalUncoveredTuple[];
  stats: GenerateResult['stats'];
  suggestions: Array<{ description: string; testCase: Record<string, string> }>;
  warnings: string[];
  strength: number;
}

function tsGenerate(input: GenerateInput): EngineResult {
  const params = toInternalParams(input.parameters);
  const opts = buildGenerateOptions(input, params);
  const result = tsGenerateRaw(opts);

  return {
    tests: result.tests.map((tc, i) => internalTestToNamed(tc, params, i)),
    negativeTests:
      result.negativeTests.length > 0
        ? result.negativeTests.map((tc, i) => internalTestToNamed(tc, params, i))
        : undefined,
    coverage: result.coverage,
    uncovered: result.uncovered,
    stats: result.stats,
    suggestions: result.suggestions.map((s) => ({
      description: s.description,
      testCase: s.testCase.values
        ? Object.fromEntries(
            params.map((p, i) => [p.name, p.values[(s.testCase as InternalTestCase).values[i]]]),
          )
        : (s.testCase as unknown as Record<string, string>),
    })),
    warnings: result.warnings,
    strength: opts.strength,
  };
}

function tsAnalyzeCoverage(
  parameters: Parameter[],
  tests: TestCase[],
  strength?: number,
): InternalCoverageReport {
  const params = toInternalParams(parameters);
  const internalTests: InternalTestCase[] = tests.map((t) => namedTestToInternal(t, params));
  const report = validateCoverage(params, internalTests, strength ?? 2);
  // Match WASM behavior: 0 tuples => coverageRatio 1.0
  if (report.totalTuples === 0) {
    report.coverageRatio = 1.0;
  }
  return report;
}

function tsExtendTests(existing: TestCase[], input: GenerateInput): EngineResult {
  const params = toInternalParams(input.parameters);
  const existingInternal = existing.map((t) => namedTestToInternal(t, params));
  const opts = buildGenerateOptions(input, params);
  const result = tsExtendRaw(existingInternal, opts);

  return {
    tests: result.tests.map((tc, i) => internalTestToNamed(tc, params, i)),
    negativeTests:
      result.negativeTests.length > 0
        ? result.negativeTests.map((tc, i) => internalTestToNamed(tc, params, i))
        : undefined,
    coverage: result.coverage,
    uncovered: result.uncovered,
    stats: result.stats,
    suggestions: [],
    warnings: result.warnings,
    strength: opts.strength,
  };
}

function tsEstimateModel(input: GenerateInput): ModelStats {
  const params = toInternalParams(input.parameters);
  const opts = buildGenerateOptions(input, params);
  return tsEstimateModelRaw(opts);
}

// ===========================================================================
// Test scenarios
// ===========================================================================

beforeAll(async () => {
  await init();
});

const scenarios: Array<{ name: string; input: GenerateInput }> = [
  {
    name: 'basic 2x2 pairwise',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
      seed: 42,
    },
  },
  {
    name: '3x3x3 pairwise',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        { name: 'arch', values: ['x64', 'arm64', 'x86'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'with constraint',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'ie'] },
      ],
      constraints: ['IF os = mac THEN browser != ie'],
      seed: 42,
    },
  },
  {
    name: 'strength 3',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
      ],
      strength: 3,
      seed: 42,
    },
  },
  {
    name: 'strength 1',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2', '3'] },
        { name: 'b', values: ['x', 'y'] },
      ],
      strength: 1,
      seed: 42,
    },
  },
  {
    name: 'maxTests limited',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2', '3', '4', '5'] },
        { name: 'b', values: ['1', '2', '3', '4', '5'] },
        { name: 'c', values: ['1', '2', '3', '4', '5'] },
      ],
      maxTests: 5,
      seed: 42,
    },
  },
  {
    name: 'with seed tests',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      seeds: [{ os: 'win', browser: 'chrome' }],
      seed: 42,
    },
  },
  {
    name: 'with invalid values',
    input: {
      parameters: [
        { name: 'browser', values: ['chrome', 'firefox', { value: 'ie6', invalid: true }] },
        { name: 'os', values: ['win', 'mac'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'with sub-models',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
        { name: 'arch', values: ['x64', 'arm64'] },
      ],
      subModels: [{ parameters: ['os', 'browser', 'arch'], strength: 3 }],
      seed: 42,
    },
  },
  {
    name: 'asymmetric parameters',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux', 'freebsd'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
        { name: 'env', values: ['dev', 'staging', 'prod'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'many parameters',
    input: {
      parameters: [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
        { name: 'c', values: ['1', '2'] },
        { name: 'd', values: ['1', '2'] },
        { name: 'e', values: ['1', '2'] },
        { name: 'f', values: ['1', '2'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'boolean and number values',
    input: {
      parameters: [
        { name: 'debug', values: [true, false] },
        { name: 'version', values: [1, 2, 3] },
        { name: 'os', values: ['win', 'mac'] },
      ],
      seed: 42,
    },
  },
  {
    name: 'different seed',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      seed: 12345,
    },
  },
  {
    name: 'with weights',
    input: {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      weights: { os: { win: 10, mac: 1, linux: 1 } },
      seed: 42,
    },
  },
  {
    name: 'single parameter',
    input: {
      parameters: [{ name: 'os', values: ['win', 'mac', 'linux'] }],
      strength: 1,
      seed: 42,
    },
  },
  // The three below carry the parameter-level features. Each is paired with a
  // feature that fills the parts of the result a projection would skip —
  // warnings, suggestions, uncovered and stats — so the whole-object gates
  // below compare them rather than passing over an empty field.
  {
    name: 'equivalence classes under a constraint',
    input: {
      parameters: [
        {
          name: 'browser',
          values: [
            { value: 'chrome', class: 'blink' },
            { value: 'edge', class: 'blink' },
            { value: 'firefox', class: 'gecko' },
          ],
        },
        {
          name: 'os',
          values: [
            { value: 'win', class: 'desktop' },
            { value: 'mac', class: 'desktop' },
            { value: 'android', class: 'mobile' },
          ],
        },
      ],
      constraints: ['IF os = android THEN browser != edge'],
      seed: 42,
    },
  },
  {
    name: 'aliased values with weights',
    input: {
      parameters: [
        {
          name: 'browser',
          values: [
            { value: 'chromium', aliases: ['chrome', 'edge'] },
            { value: 'firefox', aliases: ['gecko'] },
          ],
        },
        { name: 'os', values: ['win', 'mac', 'linux'] },
      ],
      weights: { os: { win: 5, mac: 1, linux: 1 } },
      seed: 42,
    },
  },
  {
    name: 'boundary expansion capped by maxTests',
    input: {
      parameters: [
        { name: 'port', values: [], type: 'integer', range: [1, 16], step: 1 },
        { name: 'ratio', values: [], type: 'float', range: [0, 1], step: 0.25 },
        { name: 'os', values: ['win', 'mac'] },
      ],
      maxTests: 6,
      seed: 42,
    },
  },
];

describe('WASM / TS compatibility', () => {
  describe('shipped public facade parity', () => {
    // The gates below are only as good as the table they run over: a documented
    // field no scenario uses is a field whose parity nothing compares. The
    // vocabulary is read from the published shapes rather than restated here,
    // so a field added to the surface lands as a failure with its own name in
    // it instead of as a silent gap.
    it('exercises every documented input field', () => {
      const inputFields = new Set<string>();
      const parameterFields = new Set<string>();
      const valueFields = new Set<string>();

      for (const { input } of scenarios) {
        for (const field of Object.keys(input)) {
          inputFields.add(field);
        }
        for (const parameter of input.parameters) {
          for (const field of Object.keys(parameter)) {
            parameterFields.add(field);
          }
          for (const value of parameter.values) {
            if (typeof value !== 'object') {
              continue;
            }
            for (const field of Object.keys(value)) {
              valueFields.add(field);
            }
          }
        }
      }

      const unexercised = (declared: Record<string, true>, used: Set<string>): string[] =>
        Object.keys(declared)
          .filter((field) => !used.has(field))
          .sort();

      expect(unexercised(GENERATE_INPUT_FIELDS, inputFields)).toEqual([]);
      expect(unexercised(PARAMETER_FIELDS, parameterFields)).toEqual([]);
      expect(unexercised(PARAMETER_VALUE_FIELDS, valueFields)).toEqual([]);
    });

    for (const { name, input } of scenarios) {
      it(`${name}: generate returns an identical whole public result`, () => {
        expect(pureGenerate(input)).toEqual(generate(input));
      });

      it(`${name}: estimateModel returns an identical public result`, () => {
        expect(pureEstimateModel(input)).toEqual(estimateModel(input));
      });
    }

    it('analyzeCoverage returns an identical whole public result', () => {
      const parameters: Parameter[] = [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ];
      const tests: TestCase[] = [{ os: 'win', browser: 'chrome' }];
      expect(pureAnalyzeCoverage(parameters, tests)).toEqual(analyzeCoverage(parameters, tests));
    });

    // A constraint changes two things at once on the analyze path: it shrinks
    // the tuple universe (tuples with no satisfying completion leave it) and it
    // rejects whole test rows that violate it. The pure surface parses and
    // applies constraints through its own code, so a scenario without an active
    // constraint leaves that implementation entirely unwitnessed.
    describe('constrained analyzeCoverage', () => {
      const parameters: Parameter[] = [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ];
      const constraints = ['IF os = mac THEN browser != chrome'];

      it('returns an identical whole public result for a satisfying suite', () => {
        const tests: TestCase[] = [
          { os: 'win', browser: 'chrome' },
          { os: 'mac', browser: 'safari' },
        ];
        const wasmReport = analyzeCoverage(parameters, tests, 2, constraints);
        expect(pureAnalyzeCoverage(parameters, tests, 2, constraints)).toEqual(wasmReport);

        // The constraint removes (os=mac, browser=chrome) from the universe;
        // reporting all four pairs would mean the constraint never reached the
        // enumeration.
        expect(wasmReport.totalTuples).toBe(3);
        expect(wasmReport.coveredTuples).toBe(2);
        expect(wasmReport.invalidTests).toEqual([]);
      });

      it('returns an identical whole public result for a suite with a rejected row', () => {
        const tests: TestCase[] = [
          { os: 'win', browser: 'chrome' },
          { os: 'mac', browser: 'chrome' },
          { os: 'mac', browser: 'safari' },
        ];
        const wasmReport = analyzeCoverage(parameters, tests, 2, constraints);
        expect(pureAnalyzeCoverage(parameters, tests, 2, constraints)).toEqual(wasmReport);

        // Row 1 violates the constraint, so it is rejected whole rather than
        // counted as coverage.
        expect(wasmReport.invalidTests).toHaveLength(1);
        expect(wasmReport.invalidTests[0].testIndex).toBe(1);
        expect(wasmReport.invalidTests[0].reason).toContain('violates constraint #1');
        expect(wasmReport.totalTuples).toBe(3);
        expect(wasmReport.coveredTuples).toBe(2);
      });

      it('reports an identical constraint error from both surfaces', () => {
        const bad = ['IF os = mac THEN nosuchparam != chrome'];
        const wasmError = captureError(() => analyzeCoverage(parameters, [], 2, bad));
        expect(captureError(() => pureAnalyzeCoverage(parameters, [], 2, bad))).toEqual(wasmError);
        expect(wasmError.code).toBe('CONSTRAINT_ERROR');
      });
    });

    // The tuple universe an accepted model produces is never empty, so the ratio
    // an analyze reports is always a real quotient. The inputs that would empty
    // it are rejected before enumeration, and both surfaces have to reject them
    // the same way rather than answer with a vacuous 100%.
    describe('analyzeCoverage tuple universe', () => {
      const parameters: Parameter[] = [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'safari'] },
      ];

      it('rejects a strength above the parameter count on both surfaces', () => {
        const wasmError = captureError(() => analyzeCoverage(parameters, [], 3));
        expect(captureError(() => pureAnalyzeCoverage(parameters, [], 3))).toEqual(wasmError);
        expect(wasmError.code).toBe('INVALID_INPUT');
      });

      it('rejects an unsatisfiable constraint model on both surfaces', () => {
        const contradiction = ['os = win', 'os = mac'];
        const wasmError = captureError(() => analyzeCoverage(parameters, [], 2, contradiction));
        expect(captureError(() => pureAnalyzeCoverage(parameters, [], 2, contradiction))).toEqual(
          wasmError,
        );
        expect(wasmError.code).toBe('CONSTRAINT_ERROR');
      });

      it('keeps a tuple left for the narrowest model a constraint can pin down', () => {
        // The constraint leaves exactly one satisfying assignment, so the
        // universe shrinks to the single pair that assignment contains.
        const pinned = ['os = win AND browser = chrome'];
        const wasmReport = analyzeCoverage(
          parameters,
          [{ os: 'win', browser: 'chrome' }],
          2,
          pinned,
        );
        expect(
          pureAnalyzeCoverage(parameters, [{ os: 'win', browser: 'chrome' }], 2, pinned),
        ).toEqual(wasmReport);

        expect(wasmReport.totalTuples).toBe(1);
        expect(wasmReport.coveredTuples).toBe(1);
        expect(wasmReport.coverageRatio).toBe(1);
      });
    });

    // negativeCoverage is documented as part of GenerateResult on every shipping
    // surface; the two engines fill it through separate code, so parity plus the
    // covered + omitted == total identity has to hold on both.
    describe('negativeCoverage', () => {
      const fullInput: GenerateInput = {
        parameters: [
          { name: 'A', values: ['a0', { value: 'bad', invalid: true }] },
          { name: 'B', values: ['b0', 'b1', 'b2'] },
          { name: 'C', values: ['c0', 'c1', 'c2'] },
          { name: 'D', values: ['d0', 'd1', 'd2'] },
        ],
        strength: 4,
        seed: 42,
      };

      const truncatedInput: GenerateInput = {
        parameters: [
          { name: 'A', values: ['a0', { value: 'bad', invalid: true }] },
          { name: 'B', values: ['b0', 'b1'] },
        ],
        strength: 2,
        maxTests: 3,
        seed: 42,
      };

      it('is identical on both surfaces when negative coverage completes', () => {
        const wasmResult = generate(fullInput);
        expect(pureGenerate(fullInput).negativeCoverage).toEqual(wasmResult.negativeCoverage);

        const negative = wasmResult.negativeCoverage;
        expect(negative).toBeDefined();
        if (!negative) {
          return;
        }
        expect(negative.coveredTuples + negative.omittedTuples).toBe(negative.totalTuples);
        expect(negative.omittedTuples).toBe(0);
        expect(negative.coverageRatio).toBe(1);
      });

      it('is identical on both surfaces when maxTests truncates the negative suite', () => {
        const wasmResult = generate(truncatedInput);
        expect(pureGenerate(truncatedInput).negativeCoverage).toEqual(wasmResult.negativeCoverage);

        const negative = wasmResult.negativeCoverage;
        expect(negative).toBeDefined();
        if (!negative) {
          return;
        }
        expect(negative.coveredTuples + negative.omittedTuples).toBe(negative.totalTuples);
        expect(negative.omittedTuples).toBeGreaterThan(0);
        expect(negative.coverageRatio).toBeLessThan(1);
      });
    });

    it('extendTests returns an identical whole public result', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac'] },
          { name: 'browser', values: ['chrome', 'safari'] },
        ],
        seed: 17,
      };
      const existing: TestCase[] = [{ os: 'win', browser: 'chrome' }];
      expect(pureExtendTests(existing, input)).toEqual(extendTests(existing, input));
    });

    // Both an error that carries a detail and one that does not, because the two
    // surfaces build the detail through different code paths: only an error with
    // no detail can expose the empty-string / undefined divergence.
    const errorFixtures: Array<{ label: string; input: GenerateInput; hasDetail: boolean }> = [
      {
        label: 'an error with a detail',
        input: {
          parameters: [{ name: 'A', values: ['a0'] }],
          constraints: ['missing = value'],
        } as GenerateInput,
        hasDetail: true,
      },
      {
        label: 'an error with no detail',
        input: { parameters: [] } as GenerateInput,
        hasDetail: false,
      },
    ];

    const captureError = (fn: () => unknown) => {
      try {
        fn();
      } catch (error) {
        const typed = error as { code?: unknown; message?: unknown; detail?: unknown };
        return { code: typed.code, message: typed.message, detail: typed.detail };
      }
      throw new Error('expected an error');
    };

    for (const { label, input, hasDetail } of errorFixtures) {
      it(`reports code, message, and detail identically for ${label}`, () => {
        const wasmError = captureError(() => generate(input));
        expect(captureError(() => pureGenerate(input))).toEqual(wasmError);
        if (hasDetail) {
          expect(wasmError.detail).toBeTruthy();
        } else {
          // Absent, not empty — `detail?: string` means missing, and a caller
          // branching on it must see the same value from either surface.
          expect(wasmError.detail).toBeUndefined();
        }
      });
    }
  });

  // Internal engine parity remains useful as a lower-level diagnostic, but the
  // public facade tests above are the release contract.
  describe('generate()', () => {
    for (const { name, input } of scenarios) {
      it(`${name}: coverage and stats match`, () => {
        const wasmResult = generate(input);
        const tsResult = tsGenerate(input);

        expect(tsResult.coverage).toBe(wasmResult.coverage);
        expect(tsResult.stats.totalTuples).toBe(wasmResult.stats.totalTuples);
        expect(tsResult.stats.coveredTuples).toBe(wasmResult.stats.coveredTuples);
        // Exact structural equality, not just length: a divergence in the
        // uncovered-tuple witnesses or the negative-test suite must fail parity.
        // Compare the human-readable tuple strings (the stable public contract).
        expect(tsResult.uncovered.map((u) => u.tuple)).toEqual(
          wasmResult.uncovered.map((u) => u.tuple),
        );
        expect(tsResult.negativeTests ?? []).toEqual(wasmResult.negativeTests ?? []);
        // Both engines share the same RNG, so exact suite equality (including
        // length) is asserted separately in the "exact test output match" test.
      });
    }

    // Exact output match: both engines share the xoshiro128** RNG with
    // SplitMix32 seeding and identical rejection sampling, so the same seed
    // produces a byte-identical generated suite across the WASM (C++) and
    // pure TypeScript surfaces.
    for (const { name, input } of scenarios) {
      it(`${name}: exact test output match`, () => {
        const wasmResult = generate(input);
        const tsResult = tsGenerate(input);

        expect(tsResult.tests).toEqual(wasmResult.tests);
      });
    }
  });

  describe('analyzeCoverage()', () => {
    it('full coverage: both engines agree', () => {
      const params: Parameter[] = [
        { name: 'os', values: ['win', 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ];
      const tests: TestCase[] = [
        { os: 'win', browser: 'chrome' },
        { os: 'win', browser: 'firefox' },
        { os: 'mac', browser: 'chrome' },
        { os: 'mac', browser: 'firefox' },
      ];
      const wasmReport = analyzeCoverage(params, tests);
      const tsReport = tsAnalyzeCoverage(params, tests);

      expect(tsReport.coverageRatio).toBe(wasmReport.coverageRatio);
      expect(tsReport.totalTuples).toBe(wasmReport.totalTuples);
      expect(tsReport.coveredTuples).toBe(wasmReport.coveredTuples);
      expect(tsReport.uncovered.map((u) => u.tuple)).toEqual(
        wasmReport.uncovered.map((u) => u.tuple),
      );
    });

    it('partial coverage: both engines agree', () => {
      const params: Parameter[] = [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ];
      const tests: TestCase[] = [
        { os: 'win', browser: 'chrome' },
        { os: 'mac', browser: 'firefox' },
      ];
      const wasmReport = analyzeCoverage(params, tests);
      const tsReport = tsAnalyzeCoverage(params, tests);

      expect(tsReport.coverageRatio).toBe(wasmReport.coverageRatio);
      expect(tsReport.totalTuples).toBe(wasmReport.totalTuples);
      expect(tsReport.coveredTuples).toBe(wasmReport.coveredTuples);
      // The uncovered witnesses must match exactly, not merely in count.
      expect(tsReport.uncovered.map((u) => u.tuple)).toEqual(
        wasmReport.uncovered.map((u) => u.tuple),
      );
    });

    it('empty tests: both engines agree', () => {
      const params: Parameter[] = [
        { name: 'a', values: ['1', '2'] },
        { name: 'b', values: ['1', '2'] },
      ];
      const wasmReport = analyzeCoverage(params, []);
      const tsReport = tsAnalyzeCoverage(params, []);

      expect(tsReport.coverageRatio).toBe(wasmReport.coverageRatio);
      expect(tsReport.totalTuples).toBe(wasmReport.totalTuples);
      expect(tsReport.coveredTuples).toBe(wasmReport.coveredTuples);
    });

    it('cross-engine: TS-generated tests analyzed by WASM', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const tsResult = tsGenerate(input);
      const wasmReport = analyzeCoverage(input.parameters, tsResult.tests);
      expect(wasmReport.coverageRatio).toBe(1.0);
    });

    it('cross-engine: WASM-generated tests analyzed by TS', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const wasmResult = generate(input);
      const tsReport = tsAnalyzeCoverage(input.parameters, wasmResult.tests);
      expect(tsReport.coverageRatio).toBe(1.0);
    });
  });

  describe('numeric value formatting parity', () => {
    // A parameter whose values are JSON numbers (not strings). The two surfaces
    // must render each number to a byte-identical string, or a suite generated
    // on one surface fails analyzeCoverage on the other.
    const numericInput: GenerateInput = {
      parameters: [
        { name: 'ratio', values: [3.14, 0.1, 1 / 3, 2.5] },
        { name: 'mode', values: ['fast', 'slow'] },
      ],
      seed: 7,
    };

    it('WASM-generated numeric suite analyzes to full coverage on TS', () => {
      const wasmResult = generate(numericInput);
      // WASM-formatted numeric value strings must match what the pure-JS
      // adapter expects, otherwise findValueIndex would drop coverage.
      const tsReport = tsAnalyzeCoverage(numericInput.parameters, wasmResult.tests);
      expect(tsReport.coverageRatio).toBe(1.0);
    });

    it('pure-JS-generated numeric suite analyzes to full coverage on WASM', () => {
      const pureResult = pureGenerate(numericInput);
      const wasmReport = analyzeCoverage(numericInput.parameters, pureResult.tests);
      expect(wasmReport.coverageRatio).toBe(1.0);
    });

    it('both engines emit identical numeric value strings', () => {
      const wasmResult = generate(numericInput);
      const pureResult = pureGenerate(numericInput);
      // Collect the distinct value strings each engine used for `ratio`.
      const wasmRatios = new Set(wasmResult.tests.map((t) => t.ratio));
      const pureRatios = new Set(pureResult.tests.map((t) => t.ratio));
      expect([...wasmRatios].sort()).toEqual([...pureRatios].sort());
      // The repeating-decimal value must be the shortest round-trip form.
      expect(pureRatios.has('0.3333333333333333')).toBe(true);
      expect(wasmRatios.has('0.3333333333333333')).toBe(true);
    });
  });

  describe('estimateModel()', () => {
    const estimateScenarios = scenarios.filter((s) => s.input.parameters.length > 0);

    for (const { name, input } of estimateScenarios) {
      it(`${name}: model stats match`, () => {
        const wasmStats = estimateModel(input);
        const tsStats = tsEstimateModel(input);

        expect(tsStats.parameterCount).toBe(wasmStats.parameterCount);
        expect(tsStats.totalValues).toBe(wasmStats.totalValues);
        expect(tsStats.strength).toBe(wasmStats.strength);
        expect(tsStats.totalTuples).toBe(wasmStats.totalTuples);
        expect(tsStats.subModelCount).toBe(wasmStats.subModelCount);
        expect(tsStats.constraintCount).toBe(wasmStats.constraintCount);
        expect(tsStats.parameters).toEqual(wasmStats.parameters);
      });
    }
  });

  describe('extendTests()', () => {
    it('extending partial suite: both engines achieve full coverage', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const existing: TestCase[] = [{ os: 'win', browser: 'chrome' }];

      const wasmResult = extendTests(existing, input);
      const tsResult = tsExtendTests(existing, input);

      expect(tsResult.coverage).toBe(wasmResult.coverage);
      expect(tsResult.stats.totalTuples).toBe(wasmResult.stats.totalTuples);
      expect(tsResult.stats.coveredTuples).toBe(wasmResult.stats.coveredTuples);
      // Exact suite equality: both engines share the RNG, so the extended
      // suite must be byte-identical, not merely the same length. The preserved
      // existing prefix is re-projected by the public API, so compare tests too.
      expect(tsResult.tests).toEqual(wasmResult.tests);
    });

    it('extending complete suite: both engines agree on no change needed', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac'] },
          { name: 'browser', values: ['chrome', 'firefox'] },
        ],
        seed: 42,
      };
      const wasmFull = generate(input);

      const wasmExtended = extendTests(wasmFull.tests, input);
      const tsExtended = tsExtendTests(wasmFull.tests, input);

      expect(tsExtended.coverage).toBe(1.0);
      expect(wasmExtended.coverage).toBe(1.0);
      expect(tsExtended.stats.totalTuples).toBe(wasmExtended.stats.totalTuples);
    });

    // `mode` is the one field the extend input adds to a generate input, and
    // what it does is entirely a matter of acceptance: `'strict'` is the
    // default, so naming it changes nothing about the suite, and it is the only
    // accepted value, so anything else is refused instead of being ignored.
    // Both halves need a witness -- an accepted field that changed the result
    // and a rejected one that did not reach the engine are equally defects.
    describe('extend mode', () => {
      const input: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const existing: TestCase[] = [{ os: 'win', browser: 'chrome' }];
      const strict: ExtendInput = { ...input, mode: 'strict' };

      const surfaces: Array<{
        name: string;
        extend: (existing: TestCase[], input: ExtendInput) => GenerateResult;
      }> = [
        { name: 'wasm', extend: extendTests },
        { name: 'pure', extend: pureExtendTests },
      ];

      for (const { name, extend } of surfaces) {
        it(`${name}: naming the default mode leaves the whole result unchanged`, () => {
          expect(extend(existing, strict)).toEqual(extend(existing, input));
        });

        it(`${name}: rejects a mode the engine does not implement`, () => {
          expect(() =>
            extend(existing, { ...input, mode: 'relaxed' } as unknown as ExtendInput),
          ).toThrow(/Invalid extend mode: relaxed/);
        });
      }

      it('both surfaces keep the existing prefix under an explicit strict mode', () => {
        const wasmResult = extendTests(existing, strict);
        expect(wasmResult.tests.slice(0, existing.length)).toEqual(existing);
        expect(pureExtendTests(existing, strict)).toEqual(wasmResult);
      });
    });
  });

  // The canonical seed domain is an integer in [0, 2^32 - 1]. Both the
  // WASM-backed and pure surfaces must reject the same out-of-domain values
  // with the same error shape.
  describe('seed validation parity', () => {
    const params: Parameter[] = [{ name: 'os', values: ['win', 'mac'] }];
    const validInput = (seed: number): GenerateInput => ({ parameters: params, strength: 1, seed });

    const surfaces: Array<{ name: string; gen: (input: GenerateInput) => GenerateResult }> = [
      { name: 'wasm', gen: generate },
      { name: 'pure', gen: pureGenerate },
    ];

    for (const { name, gen } of surfaces) {
      describe(name, () => {
        it('rejects a negative seed', () => {
          expect(() => gen(validInput(-1))).toThrow(/Invalid seed/);
        });

        it('rejects a non-integer seed', () => {
          expect(() => gen(validInput(1.5))).toThrow(/Invalid seed/);
        });

        it('rejects a seed at 2^32', () => {
          expect(() => gen(validInput(0x100000000))).toThrow(/Invalid seed/);
        });

        it('accepts the 2^32 - 1 boundary', () => {
          expect(() => gen(validInput(0xffffffff))).not.toThrow();
        });

        it('accepts seed 0', () => {
          expect(() => gen(validInput(0))).not.toThrow();
        });
      });
    }
  });

  // Boundary value expansion and equivalence classes must be honored identically
  // from the npm public API on both the WASM-backed and pure-JS surfaces.
  describe('boundary expansion parity', () => {
    const integerInput: GenerateInput = {
      parameters: [
        { name: 'count', values: ['10'], type: 'integer', range: [0, 5] },
        { name: 'mode', values: ['a', 'b'] },
      ],
      seed: 42,
    };

    it('WASM and pure-JS expand the same integer boundary value set', () => {
      const wasmResult = generate(integerInput);
      const pureResult = pureGenerate(integerInput);

      const wasmCounts = new Set(wasmResult.tests.map((t) => String(t.count)));
      const pureCounts = new Set(pureResult.tests.map((t) => String(t.count)));
      expect([...pureCounts].sort()).toEqual([...wasmCounts].sort());
      // min-1..min+1, max-1..max+1, merged with the original 10.
      expect([...wasmCounts].sort()).toEqual(['-1', '0', '1', '10', '4', '5', '6'].sort());
    });

    it('WASM and pure-JS agree on coverage for a float boundary parameter', () => {
      const floatInput: GenerateInput = {
        parameters: [
          { name: 'ratio', values: ['0.5'], type: 'float', range: [1, 2], step: 0.5 },
          { name: 'mode', values: ['a', 'b'] },
        ],
        seed: 42,
      };
      const wasmResult = generate(floatInput);
      const pureResult = pureGenerate(floatInput);

      // Compare the assigned ratio value strings (a test may leave a parameter
      // unassigned; WASM renders that as "" while pure-JS omits the key — an
      // incidental display difference, not a value-set difference).
      const assignedRatios = (tests: TestCase[]): string[] =>
        [...new Set(tests.map((t) => t.ratio).filter((v) => v !== undefined && v !== ''))]
          .map(String)
          .sort();
      expect(assignedRatios(pureResult.tests)).toEqual(assignedRatios(wasmResult.tests));
      // The expanded float value set: min-step..min+step, max-step..max+step.
      expect(assignedRatios(wasmResult.tests)).toEqual(['0.5', '1', '1.5', '2', '2.5'].sort());
      expect(wasmResult.coverage).toBe(pureResult.coverage);
      expect(wasmResult.stats.totalTuples).toBe(pureResult.stats.totalTuples);
    });
  });

  // Equivalence classes supplied via the public ParameterValue.class field must
  // populate result.classCoverage on both surfaces (not undefined).
  describe('equivalence class parity', () => {
    const classInput: GenerateInput = {
      parameters: [
        {
          name: 'browser',
          values: [
            { value: 'chrome', class: 'chromium' },
            { value: 'edge', class: 'chromium' },
            { value: 'firefox', class: 'gecko' },
          ],
        },
        { name: 'os', values: ['win', 'mac'] },
      ],
      seed: 42,
    };

    it('classCoverage is populated identically on WASM and pure-JS', () => {
      const wasmResult = generate(classInput);
      const pureResult = pureGenerate(classInput);

      expect(wasmResult.classCoverage).toBeDefined();
      expect(pureResult.classCoverage).toBeDefined();
      expect(pureResult.classCoverage).toEqual(wasmResult.classCoverage);
      // Sanity: at least one class tuple is tracked.
      expect(wasmResult.classCoverage?.totalClassTuples).toBeGreaterThan(0);
    });
  });

  describe('constrained class coverage parity', () => {
    const input: GenerateInput = {
      parameters: [
        {
          name: 'A',
          values: [
            { value: 'a0', class: 'c0' },
            { value: 'a1', class: 'c1' },
          ],
        },
        {
          name: 'B',
          values: [
            { value: 'b0', class: 'd0' },
            { value: 'b1', class: 'd1' },
          ],
        },
      ],
      constraints: ['IF A=a1 THEN B=b1'],
      strength: 2,
    };

    it('excludes impossible class tuples on generate and extend', () => {
      const wasmGenerated = generate(input);
      const pureGenerated = pureGenerate(input);
      expect(wasmGenerated.classCoverage).toEqual({
        totalClassTuples: 3,
        coveredClassTuples: 3,
        classCoverageRatio: 1,
      });
      expect(pureGenerated.classCoverage).toEqual(wasmGenerated.classCoverage);

      const existing = [{ A: 'a0', B: 'b0' }];
      const wasmExtended = extendTests(existing, input);
      const pureExtended = pureExtendTests(existing, input);
      expect(wasmExtended.classCoverage).toEqual(wasmGenerated.classCoverage);
      expect(pureExtended.classCoverage).toEqual(wasmGenerated.classCoverage);
    });
  });

  describe('safe TestCase identity parity', () => {
    const parameters: Parameter[] = [
      { name: '__proto__', values: ['p0', 'p1'] },
      { name: 'constructor', values: ['c0', 'c1'] },
    ];

    it('round-trips dangerous property names through generate and analyze', () => {
      const wasmGenerated = generate({ parameters });
      const pureGenerated = pureGenerate({ parameters });

      for (const result of [wasmGenerated, pureGenerated]) {
        expect(result.tests.length).toBeGreaterThan(0);
        for (const test of result.tests) {
          expect(Object.hasOwn(test, '__proto__')).toBe(true);
          expect(Object.hasOwn(test, 'constructor')).toBe(true);
        }
      }
      expect(analyzeCoverage(parameters, wasmGenerated.tests).coverageRatio).toBe(1);
      expect(pureAnalyzeCoverage(parameters, pureGenerated.tests).coverageRatio).toBe(1);
    });

    it('rejects alias-primary collisions on both surfaces', () => {
      const collision: GenerateInput = {
        parameters: [
          {
            name: 'A',
            values: [{ value: 'primary', aliases: ['collision'] }, 'collision'],
          },
          { name: 'B', values: ['0', '1'] },
        ],
      };
      expect(() => generate(collision)).toThrow(/Ambiguous value or alias/);
      expect(() => pureGenerate(collision)).toThrow(/Ambiguous value or alias/);
    });
  });

  // A constraint that references an aliased value must resolve identically on
  // both surfaces; pure-JS previously dropped aliases before generation, yielding
  // a degenerate result and a parse-failure warning.
  describe('alias + constraint parity', () => {
    const aliasInput: GenerateInput = {
      parameters: [
        {
          name: 'browser',
          values: [{ value: 'chromium', aliases: ['chrome'] }, 'firefox', 'safari'],
        },
        { name: 'os', values: ['win', 'mac'] },
      ],
      constraints: ['IF os = mac THEN browser != chrome'],
      seed: 42,
    };

    it('aliased value in a constraint yields identical, non-degenerate results', () => {
      const wasmResult = generate(aliasInput);
      const pureResult = pureGenerate(aliasInput);

      // Neither surface degraded with a constraint parse-failure warning.
      expect(wasmResult.warnings).toEqual([]);
      expect(pureResult.warnings).toEqual([]);

      // Both reach full coverage with a non-empty suite.
      expect(wasmResult.coverage).toBe(1.0);
      expect(pureResult.coverage).toBe(1.0);
      expect(pureResult.tests.length).toBeGreaterThan(0);
      expect(pureResult.coverage).toBe(wasmResult.coverage);
      expect(pureResult.stats.totalTuples).toBe(wasmResult.stats.totalTuples);

      // The constraint must hold: the chromium value (displayed as chromium or
      // its alias chrome) never pairs with os=mac.
      for (const t of pureResult.tests) {
        if (t.os === 'mac') {
          expect(t.browser === 'chromium' || t.browser === 'chrome').toBe(false);
        }
      }
    });
  });

  // The class-based Coverwise API must be a thin delegation over the free
  // functions — every method has to return a result identical to the free
  // function it wraps. The drift this guards against is the class reimplementing
  // generation, which length-only compat scenarios cannot detect.
  describe('class API parity', () => {
    it('every Coverwise method matches its free-function counterpart exactly', async () => {
      const cw = await Coverwise.create();

      for (const { name, input } of scenarios) {
        expect(cw.generate(input), `generate: ${name}`).toEqual(generate(input));
        expect(cw.estimateModel(input), `estimateModel: ${name}`).toEqual(estimateModel(input));
      }

      const analyzeParams: Parameter[] = [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ];
      const analyzeTests: TestCase[] = [
        { os: 'win', browser: 'chrome' },
        { os: 'mac', browser: 'firefox' },
      ];
      expect(cw.analyzeCoverage(analyzeParams, analyzeTests)).toEqual(
        analyzeCoverage(analyzeParams, analyzeTests),
      );

      const extendInput: GenerateInput = {
        parameters: [
          { name: 'os', values: ['win', 'mac', 'linux'] },
          { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
        ],
        seed: 42,
      };
      const existing: TestCase[] = [{ os: 'win', browser: 'chrome' }];
      expect(cw.extendTests(existing, extendInput)).toEqual(extendTests(existing, extendInput));
    });

    it('rejects invalid input through the class API with the same error', async () => {
      const cw = await Coverwise.create();
      const bad: GenerateInput = { parameters: [{ name: 'os', values: ['win'] }], seed: -1 };
      expect(() => cw.generate(bad)).toThrow(/Invalid seed/);
      expect(() => generate(bad)).toThrow(/Invalid seed/);
    });
  });

  // Absolute golden anchor: both the WASM-backed and pure-JS surfaces must emit
  // this exact suite for a fixed model+seed. The identical `tests` array is
  // pinned byte-for-byte in the C++ CLI golden test
  // (CliGenerateTest.GoldenOutputIsByteExactForFixedSeed), so all three
  // surfaces — CLI (native), WASM, pure-JS — are locked to one value. Unlike a
  // WASM-vs-pure parity check, this catches cross-drift where both engines
  // change identically.
  describe('golden output anchor', () => {
    const goldenInput: GenerateInput = {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'safari'] },
      ],
      strength: 2,
      seed: 42,
    };
    const goldenTests: TestCase[] = [
      { os: 'linux', browser: 'firefox' },
      { os: 'win', browser: 'chrome' },
      { os: 'win', browser: 'firefox' },
      { os: 'mac', browser: 'chrome' },
      { os: 'win', browser: 'safari' },
      { os: 'mac', browser: 'firefox' },
      { os: 'mac', browser: 'safari' },
      { os: 'linux', browser: 'safari' },
      { os: 'linux', browser: 'chrome' },
    ];

    it('WASM and pure-JS both match the pinned golden suite', () => {
      expect(generate(goldenInput).tests).toEqual(goldenTests);
      expect(pureGenerate(goldenInput).tests).toEqual(goldenTests);
    });
  });

  // A constraint that fails to parse must surface the same message shape on
  // every surface: the offending expression is named via an
  // `Invalid constraint "<expr>": ...` prefix, identical across WASM generate,
  // pure generate, and both analyze paths.
  describe('constraint error message parity', () => {
    const params: Parameter[] = [
      { name: 'A', values: ['a0', 'a1'] },
      { name: 'B', values: ['b0', 'b1'] },
    ];
    const badExpr = 'IF nonexistent=x THEN B!=b0';
    const prefix = `Invalid constraint "${badExpr}":`;

    const grab = (fn: () => unknown): string => {
      try {
        fn();
      } catch (e) {
        return e instanceof Error ? e.message : String(e);
      }
      throw new Error('expected a constraint error');
    };

    it('names the offending expression identically across surfaces', () => {
      const wasmGen = grab(() => generate({ parameters: params, constraints: [badExpr] }));
      const pureGen = grab(() => pureGenerate({ parameters: params, constraints: [badExpr] }));
      const wasmAnalyze = grab(() => analyzeCoverage(params, [], 2, [badExpr]));
      const pureAnalyze = grab(() => pureAnalyzeCoverage(params, [], 2, [badExpr]));

      for (const message of [wasmGen, pureGen, wasmAnalyze, pureAnalyze]) {
        expect(message.startsWith(prefix), message).toBe(true);
      }
      // The generate paths agree byte-for-byte across engines.
      expect(pureGen).toBe(wasmGen);
      // Analyze agrees byte-for-byte across engines.
      expect(pureAnalyze).toBe(wasmAnalyze);
    });
  });

  // A weight keyed by one of a value's aliases must be honored, not silently
  // dropped to the default. Resolution normalizes alias keys to the value, so an
  // alias-keyed weight produces exactly the same suite as the primary-keyed one
  // — and a suite that differs from the unweighted run (proving it took effect).
  describe('alias-keyed weight parity', () => {
    const base: GenerateInput = {
      parameters: [
        {
          name: 'browser',
          values: [{ value: 'chromium', aliases: ['chrome'] }, 'firefox', 'safari'],
        },
        { name: 'os', values: ['win', 'mac', 'linux'] },
      ],
      strength: 2,
      seed: 42,
    };
    const primaryWeighted: GenerateInput = { ...base, weights: { browser: { chromium: 10 } } };
    const aliasWeighted: GenerateInput = { ...base, weights: { browser: { chrome: 10 } } };

    for (const [label, gen] of [
      ['wasm', generate],
      ['pure', pureGenerate],
    ] as const) {
      it(`${label}: alias-keyed weight matches primary-keyed and changes output`, () => {
        const unweighted = gen(base).tests;
        const primary = gen(primaryWeighted).tests;
        const alias = gen(aliasWeighted).tests;
        // Alias key resolves to the same weight as the primary key.
        expect(alias).toEqual(primary);
        // The weight is genuinely applied (not a no-op equal to the default run).
        expect(primary).not.toEqual(unweighted);
      });
    }

    it('primary-keyed weighted suite is identical across WASM and pure', () => {
      expect(pureGenerate(primaryWeighted).tests).toEqual(generate(primaryWeighted).tests);
      expect(pureGenerate(aliasWeighted).tests).toEqual(generate(aliasWeighted).tests);
    });
  });

  describe('prototype-like weight key parity', () => {
    it('preserves a JSON __proto__ parameter key on both surfaces', () => {
      const input = JSON.parse(`{
        "parameters":[
          {"name":"__proto__","values":["a","b"]},
          {"name":"mode","values":["x","y"]}
        ],
        "strength":2,
        "seed":42,
        "weights":{"__proto__":{"a":10}}
      }`) as GenerateInput;
      expect(pureGenerate(input).tests).toEqual(generate(input).tests);
    });

    it('rejects an unknown prototype-like weight key on both surfaces', () => {
      const input = JSON.parse(`{
        "parameters":[{"name":"mode","values":["x","y"]}],
        "strength":1,
        "weights":{"__proto__":{"a":10}}
      }`) as GenerateInput;
      expect(() => pureGenerate(input)).toThrow(/Unknown parameter in weights: __proto__/);
      expect(() => generate(input)).toThrow(/Unknown parameter in weights: __proto__/);
    });
  });

  // A parameter named after an Object.prototype member must not be mistaken for
  // one carrying a boundary config: expansion regenerates the value set and drops
  // aliases and equivalence classes, which would silently contradict the model
  // while the engine still reported full coverage.
  describe('prototype-named parameter metadata parity', () => {
    const input: GenerateInput = {
      parameters: [
        {
          name: 'constructor',
          values: [
            { value: 'fast', aliases: ['quick'], class: 'speed' },
            { value: 'slow', class: 'speed' },
          ],
        },
        {
          name: '__proto__',
          values: [
            { value: 'alpha', class: 'greek' },
            { value: 'beta', class: 'greek' },
          ],
        },
        { name: 'toString', values: ['on', 'off'] },
        { name: 'valueOf', values: ['1', '2'] },
        { name: 'hasOwnProperty', values: ['yes', 'no'] },
      ],
      constraints: ['IF constructor = quick THEN toString = on'],
      strength: 2,
      seed: 42,
    };

    /** Declared value set per parameter; a row may render any declared alias. */
    const declared = new Map(
      input.parameters.map((parameter) => [
        parameter.name,
        new Set(
          parameter.values.flatMap((value) =>
            typeof value === 'object' ? [value.value, ...(value.aliases ?? [])] : [value],
          ),
        ),
      ]),
    );

    it('honors aliases and equivalence classes and reaches full coverage', () => {
      const pure = pureGenerate(input);

      expect(pure.tests.length).toBeGreaterThan(0);
      for (const test of pure.tests) {
        for (const [name, value] of Object.entries(test)) {
          expect(declared.get(name)?.has(value as string)).toBe(true);
        }
      }
      expect(pure.classCoverage).toBeDefined();
      expect(pure.classCoverage?.totalClassTuples).toBeGreaterThan(0);
      expect(pure.coverage).toBe(1);

      // The independent validator must agree with the engine's own claim.
      const report = pureAnalyzeCoverage(input.parameters, pure.tests, 2, input.constraints);
      expect(report.coverageRatio).toBe(1);
      expect(report.invalidTests).toEqual([]);
    });

    it('produces identical results on WASM and pure-JS', () => {
      const wasm = generate(input);
      const pure = pureGenerate(input);
      expect(pure.tests).toEqual(wasm.tests);
      expect(pure.coverage).toBe(wasm.coverage);
      expect(pure.classCoverage).toEqual(wasm.classCoverage);
    });
  });

  it('uses canonical aliases for suggestion text and test cases on both surfaces', () => {
    const input: GenerateInput = {
      parameters: [
        { name: 'os', values: [{ value: 'windows', aliases: ['win'] }, 'mac'] },
        { name: 'browser', values: ['chrome', 'firefox'] },
      ],
      strength: 2,
      maxTests: 1,
      seed: 42,
    };
    const wasm = generate(input);
    const pure = pureGenerate(input);
    expect(wasm.suggestions).toEqual(pure.suggestions);
    for (const suggestion of wasm.suggestions) {
      expect(suggestion.description).toContain(`os=${suggestion.testCase.os}`);
    }
  });

  // A constraint written with irregular internal whitespace must parse and
  // evaluate identically to its canonical single-space form on both engines.
  describe('irregular-whitespace constraint parity', () => {
    const spaced: GenerateInput = {
      parameters: [
        { name: 'os', values: ['win', 'mac', 'linux'] },
        { name: 'browser', values: ['chrome', 'firefox', 'ie'] },
      ],
      constraints: ['IF   os=mac   THEN   browser  !=  ie'],
      seed: 42,
    };
    const canonical: GenerateInput = {
      ...spaced,
      constraints: ['IF os = mac THEN browser != ie'],
    };

    it('irregular whitespace produces the same suite as canonical spacing', () => {
      const wasmSpaced = generate(spaced);
      const wasmCanonical = generate(canonical);
      const pureSpaced = pureGenerate(spaced);

      // No parse-failure degradation on either surface.
      expect(wasmSpaced.warnings).toEqual([]);
      expect(pureSpaced.warnings).toEqual([]);
      // Whitespace is not semantically significant: identical output either way.
      expect(wasmSpaced.tests).toEqual(wasmCanonical.tests);
      expect(pureSpaced.tests).toEqual(wasmSpaced.tests);
      // The constraint still holds.
      for (const t of wasmSpaced.tests) {
        if (t.os === 'mac') {
          expect(t.browser).not.toBe('ie');
        }
      }
    });

    it('treats the ASCII whitespace class identically across engines', () => {
      // The tokenizer whitespace class must match byte-for-byte between C++ and
      // TypeScript. Vertical tab (\v) and form feed (\f) are NOT whitespace on
      // either surface (C++ previously accepted them via locale-dependent
      // isspace, diverging from pure-JS). A constraint separated only by \v or \f
      // therefore fails to parse identically on both engines, with the same
      // thrown message.
      const messageFor = (gen: (input: GenerateInput) => unknown, sep: string): string => {
        try {
          gen({
            parameters: spaced.parameters,
            constraints: [`IF os =${sep}mac THEN browser != ie`],
            seed: 42,
          });
        } catch (e) {
          return e instanceof Error ? e.message : String(e);
        }
        throw new Error(`expected a parse error for separator ${JSON.stringify(sep)}`);
      };

      for (const sep of ['\v', '\f']) {
        expect(messageFor(pureGenerate, sep)).toBe(messageFor(generate, sep));
      }
    });
  });
});
