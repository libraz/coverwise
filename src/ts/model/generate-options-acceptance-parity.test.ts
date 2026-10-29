import { describe, expect, it } from 'vitest';
import { BoundaryType } from './boundary.js';
import { aggregateBudgetExceeded } from './budget.js';
import { ErrorCode } from './error.js';
import {
  acceptOptions,
  createGenerateOptions,
  createWeightConfig,
  type GenerateOptions,
  validateGenerateOptions,
} from './generate-options.js';
import { MAX_AGGREGATE_STRING_BYTES, MAX_STRING_BYTES } from './limits.js';

describe('string budget walk order', () => {
  // The core holds weights as std::map, so it charges weight value names in
  // byte order. A model with an oversized name inserted ahead of a smaller one
  // that tips the aggregate over must be refused over the aggregate, as the
  // core refuses it, not over whichever key the object literal listed first.
  it('charges weight value names in the core key order', () => {
    const wide = Array.from({ length: 15 }, (_unused, i) =>
      String.fromCharCode(65 + i).repeat(MAX_STRING_BYTES),
    );
    wide.push('p'.repeat(MAX_STRING_BYTES - 10));
    const weights = createWeightConfig();
    weights.entries.a = {
      ['z'.repeat(MAX_STRING_BYTES + 1)]: 1,
      ['b'.repeat(20)]: 1,
    };
    const options = createGenerateOptions({
      parameters: [
        { name: 'a', values: wide },
        { name: 'b', values: ['0', '1'] },
      ],
      weights,
    });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toBe(aggregateBudgetExceeded());
    expect(MAX_AGGREGATE_STRING_BYTES).toBe(16 * MAX_STRING_BYTES);
  });
});

describe('parameter metadata reading', () => {
  const malformed: Array<{
    label: string;
    metadata: Pick<GenerateOptions['parameters'][number], 'aliases' | 'equivalenceClasses'>;
  }> = [
    { label: 'short empty aliases', metadata: { aliases: [[]] } },
    { label: 'long empty aliases', metadata: { aliases: [[], [], []] } },
    { label: 'short empty classes', metadata: { equivalenceClasses: [''] } },
    { label: 'long empty classes', metadata: { equivalenceClasses: ['', '', ''] } },
  ];

  for (const metadata of malformed) {
    for (const boundary of [false, true]) {
      it(`rejects ${metadata.label} ${boundary ? 'with' : 'without'} boundary expansion`, () => {
        const options = createGenerateOptions({
          parameters: [
            { name: 'a', values: ['x', 'y'], ...metadata.metadata },
            { name: 'b', values: ['0', '1'] },
          ],
          ...(boundary
            ? {
                boundaryConfigs: {
                  a: { type: BoundaryType.Integer, minValue: 0, maxValue: 2, step: 1 },
                },
              }
            : {}),
        });
        expect(validateGenerateOptions(options).code).toBe(ErrorCode.InvalidInput);
        expect(acceptOptions(options).error.code).toBe(ErrorCode.InvalidInput);
      });
    }
  }
});
