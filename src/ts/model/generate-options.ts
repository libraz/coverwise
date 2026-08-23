/// Input/output data structures for test generation.

import { isNumeric } from '../util/string_util.js';
import type { BoundaryConfig } from './boundary.js';
import { BoundaryType, expandBoundaryValues } from './boundary.js';
import { ErrorCode, type ErrorInfo, okError } from './error.js';
import {
  MAX_AGGREGATE_STRING_BYTES,
  MAX_CONSTRAINTS,
  MAX_STRING_BYTES,
  MAX_TESTS,
} from './limits.js';
import { Parameter, validateParameters } from './parameter.js';
import type { TestCase } from './test-case.js';
import { utf8ByteLength } from './utf8.js';

/**
 * A sub-model: a subset of parameters with a specific coverage strength.
 *
 * Sub-models allow specifying higher coverage strength for critical parameter
 * groups. Parameters not in any sub-model are covered at the global strength.
 * A parameter may appear in multiple sub-models.
 */
export interface SubModel {
  /** Resolved to indices internally. */
  parameterNames: string[];
  /** Coverage strength for this group. */
  strength: number;
}

/**
 * Per-value weight configuration for influencing value selection.
 *
 * Higher weight = value is preferred when multiple values tie on coverage score.
 * Weight is a hint only; coverage completeness is never compromised.
 */
export interface WeightConfig {
  /** entries[paramName][valueName] = weight (default 1.0). */
  entries: Record<string, Record<string, number>>;
}

/** Get the weight for a specific parameter value. Returns 1.0 if not specified. */
export function getWeight(config: WeightConfig, paramName: string, valueName: string): number {
  if (!Object.hasOwn(config.entries, paramName)) {
    return 1.0;
  }
  const paramWeights = config.entries[paramName];
  return Object.hasOwn(paramWeights, valueName) ? paramWeights[valueName] : 1.0;
}

/** Check if any weights are configured. */
export function isWeightConfigEmpty(config: WeightConfig): boolean {
  return Object.keys(config.entries).length === 0;
}

/** Create an empty WeightConfig. */
export function createWeightConfig(): WeightConfig {
  return { entries: Object.create(null) as Record<string, Record<string, number>> };
}

/** Options for test generation. */
export interface GenerateOptions {
  parameters: Array<{
    name: string;
    values: string[];
    invalid?: boolean[];
    /** Per-value alias lists (parallel to `values`). */
    aliases?: string[][];
    /** Per-value equivalence class names (parallel to `values`). */
    equivalenceClasses?: string[];
  }>;
  /** Constraint strings. */
  constraintExpressions: string[];
  /** Interaction strength (2 = pairwise). */
  strength: number;
  /** RNG seed for deterministic output. */
  seed: number;
  /** 0 = no limit. */
  maxTests: number;
  /** Existing tests to build upon. */
  seeds: TestCase[];
  /** Mixed-strength sub-models. */
  subModels: SubModel[];
  /** Value weight hints. */
  weights: WeightConfig;
  /** Per-param boundary expansion. */
  boundaryConfigs: Record<string, BoundaryConfig>;
}

/** Create a GenerateOptions with sensible defaults. */
export function createGenerateOptions(params?: Partial<GenerateOptions>): GenerateOptions {
  return {
    parameters: params?.parameters ?? [],
    constraintExpressions: params?.constraintExpressions ?? [],
    strength: params?.strength ?? 2,
    seed: params?.seed ?? 0,
    maxTests: params?.maxTests ?? 0,
    seeds: params?.seeds ?? [],
    subModels: params?.subModels ?? [],
    weights: params?.weights ?? createWeightConfig(),
    boundaryConfigs:
      params?.boundaryConfigs ?? (Object.create(null) as Record<string, BoundaryConfig>),
  };
}

/**
 * Look up a parameter's boundary config as an own property.
 *
 * `boundaryConfigs` may be a caller-supplied plain object, so a bare index would
 * resolve inherited `Object.prototype` members and mistake a parameter named
 * `constructor` / `toString` / `valueOf` for a boundary parameter.
 */
export function getBoundaryConfig(
  options: GenerateOptions,
  paramName: string,
): BoundaryConfig | undefined {
  return Object.hasOwn(options.boundaryConfigs, paramName)
    ? options.boundaryConfigs[paramName]
    : undefined;
}

/** Whether any parameter opts into boundary expansion. */
export function hasBoundaryConfigs(options: GenerateOptions): boolean {
  return Object.keys(options.boundaryConfigs).length > 0;
}

function invalid(message: string, detail = ''): ErrorInfo {
  return { code: ErrorCode.InvalidInput, message, detail };
}

/**
 * Charge every string in the model against the documented byte budgets.
 *
 * Both the per-string and the aggregate bound are documented input limits, so
 * they belong to the acceptance contract rather than to any one surface's
 * reader. Mirrors validateStringBudget in the C++ model layer.
 */
function validateStringBudget(options: GenerateOptions, params: Parameter[]): ErrorInfo {
  let aggregate = 0;
  const account = (value: string, context: string): ErrorInfo => {
    const bytes = utf8ByteLength(value);
    if (bytes > MAX_STRING_BYTES) {
      return invalid(`${context} exceeds ${MAX_STRING_BYTES} UTF-8 bytes`);
    }
    aggregate += bytes;
    if (aggregate > MAX_AGGREGATE_STRING_BYTES) {
      return invalid(`Input strings exceed ${MAX_AGGREGATE_STRING_BYTES} UTF-8 bytes`);
    }
    return okError();
  };

  for (const param of params) {
    let error = account(param.name, `Parameter name '${param.name}'`);
    if (error.code !== ErrorCode.Ok) {
      return error;
    }
    for (let index = 0; index < param.values.length; ++index) {
      error = account(param.values[index], `${param.name}[${index}]`);
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
    for (const valueAliases of param.allAliases) {
      for (const alias of valueAliases) {
        error = account(alias, `Alias in parameter '${param.name}'`);
        if (error.code !== ErrorCode.Ok) {
          return error;
        }
      }
    }
    for (const equivalenceClass of param.equivalenceClasses) {
      error = account(equivalenceClass, `Class in parameter '${param.name}'`);
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  for (const expression of options.constraintExpressions) {
    const error = account(expression, 'Constraint expression');
    if (error.code !== ErrorCode.Ok) {
      return error;
    }
  }
  for (const subModel of options.subModels) {
    for (const name of subModel.parameterNames) {
      const error = account(name, 'Sub-model parameter name');
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  for (const [paramName, valueWeights] of Object.entries(options.weights.entries)) {
    let error = account(paramName, 'Weight parameter name');
    if (error.code !== ErrorCode.Ok) {
      return error;
    }
    for (const valueName of Object.keys(valueWeights)) {
      error = account(valueName, 'Weight value name');
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  return okError();
}

/**
 * Validate every boundary config against the declared value list.
 *
 * Runs before expansion because the checks are about the configured range and
 * the values the caller wrote down — after expansion the generated boundary
 * values would mask, for instance, a duplicate numeric identity.
 */
function validateBoundaryConfigs(
  boundaryConfigs: Record<string, BoundaryConfig>,
  params: Parameter[],
): ErrorInfo {
  const byName = new Map(params.map((param) => [param.name, param]));
  for (const [paramName, config] of Object.entries(boundaryConfigs)) {
    const param = byName.get(paramName);
    if (!param) {
      return invalid(`Unknown parameter in boundary config: ${paramName}`);
    }
    if (
      param.values.length === 0 &&
      (param.invalid.length > 0 ||
        param.allAliases.length > 0 ||
        param.equivalenceClasses.length > 0)
    ) {
      return invalid(`Metadata requires explicit values for boundary parameter ${paramName}`);
    }
    if (
      !Number.isFinite(config.minValue) ||
      !Number.isFinite(config.maxValue) ||
      config.minValue > config.maxValue
    ) {
      return invalid(`Boundary range must be finite and ordered for parameter ${paramName}`);
    }
    const boundaryValues =
      config.type === BoundaryType.Integer
        ? [
            config.minValue - 1,
            config.minValue,
            config.minValue + 1,
            config.maxValue - 1,
            config.maxValue,
            config.maxValue + 1,
          ]
        : [
            config.minValue - config.step,
            config.minValue,
            config.minValue + config.step,
            config.maxValue - config.step,
            config.maxValue,
            config.maxValue + config.step,
          ];
    if (boundaryValues.some((value) => !Number.isFinite(value))) {
      return invalid(`Boundary expansion must produce finite values for parameter ${paramName}`);
    }
    const numericIdentities = new Set<number>();
    for (const value of param.values) {
      if (!isNumeric(value)) {
        continue;
      }
      const numeric = Number(value);
      if (!Number.isFinite(numeric)) {
        return invalid(
          `Boundary parameter contains a non-finite numeric value: ${paramName}=${value}`,
        );
      }
      if (numericIdentities.has(numeric)) {
        return invalid(`Boundary parameter contains duplicate numeric identities: ${paramName}`);
      }
      numericIdentities.add(numeric);
    }
    if (config.type === BoundaryType.Float) {
      if (!Number.isFinite(config.step) || config.step <= 0) {
        return invalid(`Boundary step must be finite and positive for parameter ${paramName}`);
      }
    } else {
      // Integer expansion always steps by one, so a caller asking for anything
      // else is asking for a value set the engine will not produce. Rejecting is
      // the only answer that keeps the model JSON meaning one thing everywhere.
      if (config.step !== 1) {
        return invalid(`Integer boundary step must be 1 for parameter ${paramName}`);
      }
      if (!Number.isSafeInteger(config.minValue) || !Number.isSafeInteger(config.maxValue)) {
        return invalid(`Integer boundary endpoints must be safe integers for ${paramName}`);
      }
      // Gate on the shared isNumeric predicate (same as the identity loop above
      // and the C++ core) so both surfaces classify a value as numeric — and
      // therefore range-check it — identically.
      for (const value of param.values) {
        if (!isNumeric(value)) {
          continue;
        }
        if (!Number.isSafeInteger(Number(value))) {
          return invalid(
            `Integer boundary parameter contains a non-integral or out-of-range value: ${paramName}=${value}`,
          );
        }
      }
    }
  }
  return okError();
}

/**
 * Validate every boundary config and expand the parameters it covers.
 *
 * Surfaces call this before resolving `seeds` / `existing` rows, which need the
 * final value list to map a value name to an index, and before judging the
 * parameter set — the rules apply to the value space the engine will use, not to
 * the shorter list the caller wrote down. Mirrors ExpandBoundaries in the C++
 * model layer.
 *
 * @param params - Parameters to expand; not modified.
 * @param boundaryConfigs - Config per parameter name.
 * @returns The expanded parameters, and an error describing the first malformed
 *   config (in which case the parameters are returned unchanged).
 */
export function expandBoundaries(
  params: Parameter[],
  boundaryConfigs: Record<string, BoundaryConfig>,
): { params: Parameter[]; error: ErrorInfo } {
  if (Object.keys(boundaryConfigs).length === 0) {
    return { params, error: okError() };
  }
  const configError = validateBoundaryConfigs(boundaryConfigs, params);
  if (configError.code !== ErrorCode.Ok) {
    return { params, error: configError };
  }
  const expanded = params.map((param) =>
    Object.hasOwn(boundaryConfigs, param.name)
      ? expandBoundaryValues(param, boundaryConfigs[param.name])
      : param,
  );
  return { params: expanded, error: okError() };
}

/** Validate generation options before expansion or resource allocation. */
export function validateGenerateOptions(options: GenerateOptions): ErrorInfo {
  const params = options.parameters.map((input) => {
    const param = input.invalid
      ? new Parameter(input.name, input.values, input.invalid)
      : new Parameter(input.name, input.values);
    if (input.aliases) {
      param.setAliases(input.aliases);
    }
    if (input.equivalenceClasses) {
      param.setEquivalenceClasses(input.equivalenceClasses);
    }
    return param;
  });
  const budgetError = validateStringBudget(options, params);
  if (budgetError.code !== ErrorCode.Ok) {
    return budgetError;
  }
  const boundaryError = validateBoundaryConfigs(options.boundaryConfigs, params);
  if (boundaryError.code !== ErrorCode.Ok) {
    return boundaryError;
  }
  const validationParams = params.map((param) => {
    if (param.values.length > 0 || getBoundaryConfig(options, param.name) === undefined) {
      return param;
    }
    return new Parameter(param.name, ['__coverwise_boundary_placeholder__']);
  });
  const parameterError = validateParameters(validationParams);
  if (parameterError.length > 0) {
    return invalid(parameterError);
  }
  if (params.length === 0) {
    return invalid('At least one parameter is required');
  }
  if (
    !Number.isInteger(options.strength) ||
    options.strength < 1 ||
    options.strength > params.length
  ) {
    return invalid(
      'Strength must be between 1 and parameter count',
      `strength=${options.strength}, parameters=${params.length}`,
    );
  }
  if (!Number.isInteger(options.seed) || options.seed < 0 || options.seed > 0xffffffff) {
    return invalid('Seed must be an integer in [0, 4294967295]');
  }
  if (!Number.isInteger(options.maxTests) || options.maxTests < 0) {
    return invalid('maxTests must be a non-negative integer');
  }
  if (options.constraintExpressions.length > MAX_CONSTRAINTS) {
    return invalid(
      `Constraint count ${options.constraintExpressions.length} exceeds maximum of ${MAX_CONSTRAINTS}`,
    );
  }
  if (options.seeds.length > MAX_TESTS) {
    return invalid(`Seed test count ${options.seeds.length} exceeds maximum of ${MAX_TESTS}`);
  }

  const byName = new Map(params.map((param) => [param.name, param]));
  for (const subModel of options.subModels) {
    if (subModel.parameterNames.length === 0) {
      return invalid('Sub-model must contain at least one parameter');
    }
    const seen = new Set<string>();
    for (const name of subModel.parameterNames) {
      if (seen.has(name)) {
        return invalid(`Duplicate parameter in sub-model: ${name}`);
      }
      if (!byName.has(name)) {
        return invalid(`Unknown parameter in sub-model: ${name}`);
      }
      seen.add(name);
    }
    if (
      !Number.isInteger(subModel.strength) ||
      subModel.strength < 1 ||
      subModel.strength > subModel.parameterNames.length
    ) {
      return invalid('Sub-model strength must be between 1 and its parameter count');
    }
  }

  for (const [paramName, valueWeights] of Object.entries(options.weights.entries)) {
    const param = byName.get(paramName);
    if (!param) {
      return invalid(`Unknown parameter in weights: ${paramName}`);
    }
    for (const [valueName, weight] of Object.entries(valueWeights)) {
      if (param.findValueIndex(valueName) === 0xffffffff) {
        return invalid(`Unknown value in weights: ${paramName}=${valueName}`);
      }
      if (!Number.isFinite(weight) || weight <= 0) {
        return invalid(`Weight must be finite and positive: ${paramName}=${valueName}`);
      }
    }
  }
  return okError();
}

/** Mode for extendTests operation. */
export enum ExtendMode {
  /** Keep existing tests exactly as-is. */
  Strict = 'strict',
}

/** Per-parameter detail for ModelStats. */
export interface ParamDetail {
  name: string;
  valueCount: number;
  invalidCount: number;
}

/** Model statistics for preview without running generation. */
export interface ModelStats {
  parameterCount: number;
  totalValues: number;
  strength: number;
  /** Raw global + sub-model tuple upper bound before constraint exclusion. */
  totalTuples: number;
  /**
   * Coarse sizing heuristic from the largest value count, the strength and the
   * parameter count, capped at `totalTuples`. Not a bound in either direction:
   * a generated suite may be smaller or larger.
   */
  estimatedTests: number;
  subModelCount: number;
  constraintCount: number;
  parameters: ParamDetail[];
  error: ErrorInfo;
}

/** Create a default ModelStats. */
export function createModelStats(): ModelStats {
  return {
    parameterCount: 0,
    totalValues: 0,
    strength: 0,
    totalTuples: 0,
    estimatedTests: 0,
    subModelCount: 0,
    constraintCount: 0,
    parameters: [],
    error: okError(),
  };
}
