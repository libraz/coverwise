/// Input/output data structures for test generation.

import type { BoundaryConfig } from './boundary.js';
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
  const paramWeights = config.entries[paramName];
  if (paramWeights === undefined) {
    return 1.0;
  }
  const w = paramWeights[valueName];
  return w === undefined ? 1.0 : w;
}

/** Check if any weights are configured. */
export function isWeightConfigEmpty(config: WeightConfig): boolean {
  return Object.keys(config.entries).length === 0;
}

/** Create an empty WeightConfig. */
export function createWeightConfig(): WeightConfig {
  return { entries: {} };
}

/** Options for test generation. */
export interface GenerateOptions {
  parameters: Array<{ name: string; values: string[] }>;
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
  totalTuples: number;
  estimatedTests: number;
  subModelCount: number;
  constraintCount: number;
  parameters: ParamDetail[];
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
  };
}
