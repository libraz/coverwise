/// Input/output data structures for test generation.

import { isNumeric } from '../util/string_util.js';
import type { BoundaryConfig } from './boundary.js';
import { BoundaryType } from './boundary.js';
import { ErrorCode, type ErrorInfo, okError } from './error.js';
import { Parameter, validateParameters } from './parameter.js';
import type { TestCase } from './test-case.js';

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
    boundaryConfigs: params?.boundaryConfigs ?? {},
  };
}

function invalid(message: string, detail = ''): ErrorInfo {
  return { code: ErrorCode.InvalidInput, message, detail };
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
  const validationParams = params.map((param) => {
    if (param.values.length > 0 || options.boundaryConfigs[param.name] === undefined) {
      return param;
    }
    return new Parameter(param.name, ['__coverwise_boundary_placeholder__']);
  });
  for (const param of params) {
    if (
      param.values.length === 0 &&
      options.boundaryConfigs[param.name] !== undefined &&
      (param.invalid.length > 0 ||
        param.allAliases.length > 0 ||
        param.equivalenceClasses.length > 0)
    ) {
      return invalid(`Metadata requires explicit values for boundary parameter ${param.name}`);
    }
  }
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

  for (const [paramName, config] of Object.entries(options.boundaryConfigs)) {
    const param = byName.get(paramName);
    if (!param) {
      return invalid(`Unknown parameter in boundary config: ${paramName}`);
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
