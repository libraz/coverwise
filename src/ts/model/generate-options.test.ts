import { describe, expect, it } from 'vitest';
import { createBoundaryConfig } from './boundary.js';
import { ErrorCode } from './error.js';
import {
  createGenerateOptions,
  createModelStats,
  createWeightConfig,
  expandBoundaries,
  getBoundaryConfig,
  getWeight,
  hasBoundaryConfigs,
  isWeightConfigEmpty,
  validateGenerateOptions,
} from './generate-options.js';
import {
  MAX_AGGREGATE_STRING_BYTES,
  MAX_CONSTRAINTS,
  MAX_STRING_BYTES,
  MAX_VALUES_PER_PARAMETER,
} from './limits.js';
import { Parameter } from './parameter.js';

describe('createGenerateOptions', () => {
  it('creates options with sensible defaults', () => {
    const opts = createGenerateOptions();
    expect(opts.parameters).toEqual([]);
    expect(opts.constraintExpressions).toEqual([]);
    expect(opts.strength).toBe(2);
    expect(opts.seed).toBe(0);
    expect(opts.maxTests).toBe(0);
    expect(opts.seeds).toEqual([]);
    expect(opts.subModels).toEqual([]);
    expect(opts.weights.entries).toEqual({});
    expect(opts.boundaryConfigs).toEqual({});
  });

  it('applies partial overrides', () => {
    const opts = createGenerateOptions({
      strength: 3,
      seed: 42,
      parameters: [{ name: 'os', values: ['win', 'mac'] }],
    });
    expect(opts.strength).toBe(3);
    expect(opts.seed).toBe(42);
    expect(opts.parameters).toEqual([{ name: 'os', values: ['win', 'mac'] }]);
    // Non-overridden fields keep defaults.
    expect(opts.constraintExpressions).toEqual([]);
    expect(opts.maxTests).toBe(0);
  });

  it('accepts weight config override', () => {
    const weights = { entries: { os: { win: 2.0 } } };
    const opts = createGenerateOptions({ weights });
    expect(opts.weights.entries.os.win).toBe(2.0);
  });
});

describe('getWeight', () => {
  it('returns configured weight', () => {
    const config = { entries: { os: { win: 2.5, mac: 0.5 } } };
    expect(getWeight(config, 'os', 'win')).toBe(2.5);
    expect(getWeight(config, 'os', 'mac')).toBe(0.5);
  });

  it('returns 1.0 for missing parameter', () => {
    const config = { entries: { os: { win: 2.0 } } };
    expect(getWeight(config, 'browser', 'chrome')).toBe(1.0);
  });

  it('returns 1.0 for missing value within configured parameter', () => {
    const config = { entries: { os: { win: 2.0 } } };
    expect(getWeight(config, 'os', 'linux')).toBe(1.0);
  });
});

describe('isWeightConfigEmpty', () => {
  it('returns true for empty config', () => {
    expect(isWeightConfigEmpty(createWeightConfig())).toBe(true);
  });

  it('returns false for non-empty config', () => {
    const config = { entries: { os: { win: 2.0 } } };
    expect(isWeightConfigEmpty(config)).toBe(false);
  });
});

describe('createWeightConfig', () => {
  it('creates config with empty entries', () => {
    const config = createWeightConfig();
    expect(config.entries).toEqual({});
  });
});

describe('getBoundaryConfig', () => {
  const prototypeNames = ['constructor', 'toString', 'valueOf', 'hasOwnProperty', '__proto__'];

  it('reports no config for Object.prototype member names by default', () => {
    const opts = createGenerateOptions();
    for (const name of prototypeNames) {
      expect(getBoundaryConfig(opts, name)).toBeUndefined();
    }
    expect(hasBoundaryConfigs(opts)).toBe(false);
  });

  it('ignores inherited members of a caller-supplied plain object', () => {
    const opts = createGenerateOptions({ boundaryConfigs: {} });
    for (const name of prototypeNames) {
      expect(getBoundaryConfig(opts, name)).toBeUndefined();
    }
    expect(hasBoundaryConfigs(opts)).toBe(false);
  });

  it('returns an own entry, including one keyed by a prototype member name', () => {
    const config = createBoundaryConfig({ minValue: 0, maxValue: 10 });
    const opts = createGenerateOptions({ boundaryConfigs: { constructor: config } });
    expect(getBoundaryConfig(opts, 'constructor')).toBe(config);
    expect(getBoundaryConfig(opts, 'toString')).toBeUndefined();
    expect(hasBoundaryConfigs(opts)).toBe(true);
  });
});

describe('createModelStats', () => {
  it('creates stats with default zeros', () => {
    const stats = createModelStats();
    expect(stats.parameterCount).toBe(0);
    expect(stats.totalValues).toBe(0);
    expect(stats.strength).toBe(0);
    expect(stats.totalTuples).toBe(0);
    expect(stats.estimatedTests).toBe(0);
    expect(stats.subModelCount).toBe(0);
    expect(stats.constraintCount).toBe(0);
    expect(stats.parameters).toEqual([]);
  });
});

describe('expandBoundaries', () => {
  it('validates the configs and expands the parameters they cover', () => {
    const params = [new Parameter('n', []), new Parameter('m', ['a', 'b'])];
    const result = expandBoundaries(params, {
      n: createBoundaryConfig({ minValue: 0, maxValue: 10 }),
    });
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.params[0].values).toEqual(['-1', '0', '1', '9', '10', '11']);
    expect(result.params[1].values).toEqual(['a', 'b']);
  });

  // Integer expansion steps by one, so any other step describes a value set the
  // engine will not produce. The message is byte-identical to the C++ gate's.
  it('rejects an integer step other than 1', () => {
    const params = [new Parameter('n', [])];
    const result = expandBoundaries(params, {
      n: createBoundaryConfig({ minValue: 0, maxValue: 10, step: 5 }),
    });
    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toBe('Integer boundary step must be 1 for parameter n');
  });

  it('leaves the parameters alone when a config is malformed', () => {
    const params = [new Parameter('n', [])];
    const result = expandBoundaries(params, {
      n: createBoundaryConfig({ minValue: 10, maxValue: 0 }),
    });
    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.params[0].values).toEqual([]);
  });
});

describe('validateGenerateOptions acceptance limits', () => {
  const twoBinary = [
    { name: 'a', values: ['0', '1'] },
    { name: 'b', values: ['0', '1'] },
  ];

  // A boundary parameter may spell out only the values it wants marked invalid;
  // the range supplies the valid ones. Expansion runs before the model is
  // judged, so the parameter is well-formed rather than valueless.
  it('accepts a boundary parameter whose only declared value is invalid', () => {
    const params = [new Parameter('age', ['999'], [true]), new Parameter('mode', ['a', 'b'])];
    const expansion = expandBoundaries(params, {
      age: createBoundaryConfig({ minValue: 0, maxValue: 10 }),
    });
    expect(expansion.error.code).toBe(ErrorCode.Ok);
    const options = createGenerateOptions({
      parameters: expansion.params.map((param) => ({
        name: param.name,
        values: param.values,
        invalid: param.invalid,
      })),
    });
    expect(validateGenerateOptions(options).code).toBe(ErrorCode.Ok);
  });

  it('rejects more values than one parameter may declare', () => {
    const values = Array.from({ length: MAX_VALUES_PER_PARAMETER + 1 }, (_unused, i) => String(i));
    const options = createGenerateOptions({ parameters: [...twoBinary, { name: 'wide', values }] });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toContain('has too many values');
  });

  it('rejects more constraints than one model may carry', () => {
    const options = createGenerateOptions({
      parameters: twoBinary,
      constraintExpressions: Array.from({ length: MAX_CONSTRAINTS + 1 }, () => 'a = 0'),
    });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toContain('exceeds maximum');
  });

  it('rejects a single string beyond the per-string budget', () => {
    const options = createGenerateOptions({
      parameters: [...twoBinary, { name: 'wide', values: ['x'.repeat(MAX_STRING_BYTES + 1)] }],
    });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toContain('UTF-8 bytes');
  });

  it('rejects string data beyond the aggregate budget', () => {
    const valueCount = Math.floor(MAX_AGGREGATE_STRING_BYTES / MAX_STRING_BYTES) + 2;
    const values = Array.from(
      { length: valueCount },
      (_unused, i) => 'x'.repeat(MAX_STRING_BYTES - 8) + String(1000000 + i),
    );
    const options = createGenerateOptions({ parameters: [...twoBinary, { name: 'wide', values }] });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toBe(`Input strings exceed ${MAX_AGGREGATE_STRING_BYTES} UTF-8 bytes`);
  });
});
