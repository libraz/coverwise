import { describe, expect, it } from 'vitest';
import { aggregateBudgetExceeded } from './budget.js';
import { ErrorCode } from './error.js';
import {
  acceptOptions,
  createGenerateOptions,
  createWeightConfig,
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
  // An alias list with no aliases in it carries no metadata, and the rule set
  // must read it the same way whether it is reached directly or through the
  // acceptance gate.
  it('treats an all-empty alias list the same through both entry points', () => {
    const options = createGenerateOptions({
      parameters: [
        { name: 'a', values: ['x', 'y'], aliases: [[]] },
        { name: 'b', values: ['0', '1'] },
      ],
    });
    expect(validateGenerateOptions(options).code).toBe(acceptOptions(options).error.code);
  });
});
