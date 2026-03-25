/// Boundary value expansion for numeric parameters.

import { isNumeric } from '../util/string_util.js';
import { Parameter } from './parameter.js';

/** Boundary value type: integer or float. */
export enum BoundaryType {
  Integer = 'integer',
  Float = 'float',
}

/** Configuration for boundary value expansion of a numeric parameter. */
export interface BoundaryConfig {
  type: BoundaryType;
  minValue: number;
  maxValue: number;
  /** Step size for float type (default 1.0 for integer). */
  step: number;
}

/** Create a default BoundaryConfig. */
export function createBoundaryConfig(params?: Partial<BoundaryConfig>): BoundaryConfig {
  return {
    type: params?.type ?? BoundaryType.Integer,
    minValue: params?.minValue ?? 0,
    maxValue: params?.maxValue ?? 0,
    step: params?.step ?? 1.0,
  };
}

/** Format an integer value as a string. */
function formatInteger(value: number): string {
  return String(Math.round(value));
}

/** Format a float value as a string, trimming trailing zeros. */
function formatFloat(value: number): string {
  return String(value);
}

/**
 * Expand a parameter's values to include boundary values.
 *
 * For integer type, adds: min-1, min, min+1, max-1, max, max+1.
 * For float type, adds: min-step, min, min+step, max-step, max, max+step.
 * Merges with existing values (dedup) and sorts numerically.
 *
 * @param param The original parameter.
 * @param config The boundary configuration specifying the range.
 * @returns A new Parameter with expanded values.
 */
export function expandBoundaryValues(param: Parameter, config: BoundaryConfig): Parameter {
  // Generate boundary values.
  let boundaryNums: number[];
  if (config.type === BoundaryType.Integer) {
    const step = 1.0;
    boundaryNums = [
      config.minValue - step,
      config.minValue,
      config.minValue + step,
      config.maxValue - step,
      config.maxValue,
      config.maxValue + step,
    ];
  } else {
    boundaryNums = [
      config.minValue - config.step,
      config.minValue,
      config.minValue + config.step,
      config.maxValue - config.step,
      config.maxValue,
      config.maxValue + config.step,
    ];
  }

  // Collect existing values as numbers (for dedup), and track non-numeric values.
  const seenNums = new Set<number>();
  const nonNumericValues: string[] = [];
  for (const v of param.values) {
    if (isNumeric(v)) {
      seenNums.add(Number(v));
    } else {
      nonNumericValues.push(v);
    }
  }

  // Add boundary values (dedup).
  for (const bv of boundaryNums) {
    seenNums.add(bv);
  }

  // Sort numerically and format.
  const sortedNums = Array.from(seenNums).sort((a, b) => a - b);

  const expandedValues: string[] = [];
  for (const d of sortedNums) {
    if (config.type === BoundaryType.Integer) {
      expandedValues.push(formatInteger(d));
    } else {
      expandedValues.push(formatFloat(d));
    }
  }

  // Append non-numeric values at the end.
  for (const v of nonNumericValues) {
    expandedValues.push(v);
  }

  // Build the result parameter. Preserve name, drop invalid/aliases since
  // boundary expansion changes the value set.
  return new Parameter(param.name, expandedValues);
}
