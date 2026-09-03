import { describe, expect, it } from 'vitest';
import { createBoundaryConfig } from './boundary.js';
import { boundaryAcceptanceError } from './boundary-rules.js';
import { aggregateBudgetExceeded } from './budget.js';
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

  // A config naming a parameter the model does not declare describes a value
  // space for nothing. The public surfaces key their configs by the parameter
  // they read them from and so cannot produce one, which leaves this rule
  // reachable only from here.
  it('rejects a config for a parameter the model does not declare', () => {
    const result = expandBoundaries([new Parameter('n', ['1'])], {
      other: createBoundaryConfig({ minValue: 0, maxValue: 10 }),
    });
    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toBe(boundaryAcceptanceError.unknownParameter('other'));
  });

  // Metadata runs parallel to the value list, and expansion rebuilds it against
  // the value set it produces — so a length that never matched is silently made
  // to match, and the flags land on values the caller never marked. The public
  // surfaces build these arrays themselves and keep them parallel, so this is a
  // rule about the core rather than about a reachable input: what it pins is
  // that the pure port refuses what the C++ core refuses.
  it('rejects metadata whose length disagrees with the value list', () => {
    const cases: Array<{ field: string; build: () => Parameter }> = [
      {
        field: 'invalid',
        build: () => new Parameter('n', ['5'], [true, false]),
      },
      {
        field: 'aliases',
        build: () => {
          const param = new Parameter('n', ['5']);
          param.setAliases([['five'], ['six']]);
          return param;
        },
      },
      {
        field: 'equivalence classes',
        build: () => {
          const param = new Parameter('n', ['5']);
          param.setEquivalenceClasses(['low', 'high']);
          return param;
        },
      },
    ];

    for (const { field, build } of cases) {
      const result = expandBoundaries([build()], {
        n: createBoundaryConfig({ minValue: 0, maxValue: 10 }),
      });
      expect(result.error.code, field).toBe(ErrorCode.InvalidInput);
      expect(result.error.message, field).toBe(boundaryAcceptanceError.metadataLength('n', field));
      expect(result.params[0].values, field).toEqual(['5']);
    }
  });

  it('expands a parameter whose metadata runs parallel to its values', () => {
    const param = new Parameter('n', ['5'], [true]);
    const result = expandBoundaries([param], {
      n: createBoundaryConfig({ minValue: 0, maxValue: 10 }),
    });
    expect(result.error.code).toBe(ErrorCode.Ok);
    expect(result.params[0].values).toEqual(['-1', '0', '1', '5', '9', '10', '11']);
    expect(result.params[0].invalid).toEqual([false, false, false, true, false, false, false]);
  });

  // Per-value metadata is positional, so a parameter that declares none of its
  // values has nothing for it to attach to: expansion would generate the values
  // and the flags would land on whichever ones happened to be produced.
  it('rejects per-value metadata on a parameter that declares no values', () => {
    const param = new Parameter('n', []);
    param.setInvalid([true]);
    const result = expandBoundaries([param], {
      n: createBoundaryConfig({ minValue: 0, maxValue: 10 }),
    });
    expect(result.error.code).toBe(ErrorCode.InvalidInput);
    expect(result.error.message).toBe(boundaryAcceptanceError.metadataWithoutValues('n'));
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
    expect(error.message).toBe(aggregateBudgetExceeded());
  });

  // Budgets are byte budgets. A model measured in characters would accept
  // several times the documented text whenever the caller writes outside ASCII,
  // which is the case the limit exists for.
  it('charges non-ASCII text by its UTF-8 length', () => {
    const fourBytesPerCharacter = '𝒳';
    const charactersPerValue = Math.floor(MAX_STRING_BYTES / 4) - 1;
    const valueCount = Math.floor(MAX_AGGREGATE_STRING_BYTES / (charactersPerValue * 4)) + 1;
    const values = Array.from(
      { length: valueCount },
      (_unused, i) => fourBytesPerCharacter.repeat(charactersPerValue) + String(i),
    );
    const options = createGenerateOptions({ parameters: [...twoBinary, { name: 'wide', values }] });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toBe(aggregateBudgetExceeded());
  });

  // A row reaches the engine as value indices, so a suite of resolved rows is
  // free. What a row does carry is the text of any position that did not
  // resolve, which the diagnostics quote back, so that text is charged like any
  // other caller string.
  it('spends nothing on rows whose every position resolved', () => {
    const seeds = Array.from({ length: 1000 }, () => ({ values: [0, 1] }));
    const options = createGenerateOptions({ parameters: twoBinary, seeds });
    expect(validateGenerateOptions(options).code).toBe(ErrorCode.Ok);
  });

  it('charges the text of a row position that did not resolve', () => {
    const text = 'x'.repeat(60 * 1024);
    const rowCount = Math.ceil(MAX_AGGREGATE_STRING_BYTES / (2 * text.length)) + 1;
    const seeds = Array.from({ length: rowCount }, () => ({
      values: [0xffffffff, 0xffffffff],
      unresolved: [text, text],
    }));
    const options = createGenerateOptions({ parameters: twoBinary, seeds });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toBe(aggregateBudgetExceeded());
  });

  it('names the row a too-large unresolved value came from', () => {
    const seeds = [
      { values: [0, 1] },
      { values: [0xffffffff, 1], unresolved: ['x'.repeat(MAX_STRING_BYTES + 1), ''] },
    ];
    const options = createGenerateOptions({ parameters: twoBinary, seeds });
    const error = validateGenerateOptions(options);
    expect(error.code).toBe(ErrorCode.InvalidInput);
    expect(error.message).toBe(`Value in seeds row 1 exceeds ${MAX_STRING_BYTES} UTF-8 bytes`);
  });
});
