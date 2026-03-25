import { describe, expect, it } from 'vitest';
import {
  createGenerateOptions,
  createModelStats,
  createWeightConfig,
  getWeight,
  isWeightConfigEmpty,
} from './generate-options.js';

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
