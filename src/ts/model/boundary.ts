/// Boundary value expansion for numeric parameters.

import { isNumeric, jsNumberToString } from '../util/string_util.js';
import { DEFAULT_BOUNDARY_STEP, INTEGER_BOUNDARY_STEP } from './boundary-rules.js';
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
    step: params?.step ?? DEFAULT_BOUNDARY_STEP,
  };
}

/** Format an integer value as a string (matches C++ JsNumberToString). */
function formatInteger(value: number): string {
  return jsNumberToString(Math.round(value));
}

/** Format a float value as the shortest round-trip string. */
function formatFloat(value: number): string {
  return jsNumberToString(value);
}

/** Number of decimal places in the shortest round-trip rendering of `value`. */
function decimalPlaces(value: number): number {
  const text = jsNumberToString(Math.abs(value));
  const exponentAt = text.indexOf('e');
  const mantissa = exponentAt === -1 ? text : text.slice(0, exponentAt);
  const dot = mantissa.indexOf('.');
  let places = dot === -1 ? 0 : mantissa.length - dot - 1;
  if (exponentAt !== -1) {
    places -= Number.parseInt(text.slice(exponentAt + 1), 10);
  }
  return Math.max(places, 0);
}

/** Round `value` to `places` decimals, so accumulated binary error does not leak. */
function snapToDecimals(value: number, places: number): number {
  if (places > 15) {
    return value;
  }
  let scale = 1;
  for (let i = 0; i < places; i++) {
    scale *= 10;
  }
  const scaled = value * scale;
  if (!(Math.abs(scaled) < 9007199254740992)) {
    return value;
  }
  return Math.floor(scaled + 0.5) / scale;
}

/**
 * Expand a parameter's values to include boundary values.
 *
 * For integer type, adds: min-1, min, min+1, max-1, max, max+1.
 * For float type, adds: min-step, min, min+step, max-step, max, max+step.
 * Merges with existing values (dedup) and sorts numerically. The generated
 * values outside [min, max] are marked invalid, so they feed single-fault
 * negative tests; a value the caller declared keeps its own metadata.
 *
 * @param param The original parameter.
 * @param config The boundary configuration specifying the range.
 * @returns A new Parameter with expanded values.
 */
export function expandBoundaryValues(param: Parameter, config: BoundaryConfig): Parameter {
  // Generate boundary values.
  let boundaryNums: number[];
  if (config.type === BoundaryType.Integer) {
    const step = INTEGER_BOUNDARY_STEP;
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
    // Snap to the precision the caller wrote, so 0.3 - 0.1 is 0.2 rather than 0.19999999999999998.
    const places = Math.max(
      decimalPlaces(config.minValue),
      decimalPlaces(config.maxValue),
      decimalPlaces(config.step),
    );
    boundaryNums = boundaryNums.map((bv) =>
      Number.isFinite(bv) ? snapToDecimals(bv, places) : bv,
    );
  }

  // Keep the original value index for each numeric identity so spelling and
  // per-value metadata survive deduplication with generated boundaries.
  const numericValues = new Map<number, number | null>();
  const nonNumericIndices: number[] = [];
  for (let i = 0; i < param.values.length; ++i) {
    const v = param.values[i];
    const numeric = isNumeric(v) ? Number(v) : Number.NaN;
    if (Number.isFinite(numeric)) {
      if (!numericValues.has(numeric)) {
        numericValues.set(numeric, i);
      }
    } else {
      nonNumericIndices.push(i);
    }
  }

  // Add boundary values (dedup).
  for (const bv of boundaryNums) {
    if (Number.isFinite(bv) && !numericValues.has(bv)) {
      numericValues.set(bv, null);
    }
  }

  // Sort numerically and format.
  const sortedNums = Array.from(numericValues.keys()).sort((a, b) => a - b);

  const expandedValues: string[] = [];
  const expandedInvalid: boolean[] = [];
  const expandedAliases: string[][] = [];
  const expandedClasses: string[] = [];
  const outOfRange = (value: number): boolean => value < config.minValue || value > config.maxValue;
  const generatedOutOfRange = sortedNums.some(
    (value) => numericValues.get(value) === null && outOfRange(value),
  );
  const preserveInvalid = param.invalid.length > 0 || generatedOutOfRange;
  const preserveAliases = param.allAliases.length > 0;
  const preserveClasses = param.equivalenceClasses.length > 0;

  const appendMetadata = (original: number | null, value: number): void => {
    if (preserveInvalid) {
      expandedInvalid.push(
        original === null ? outOfRange(value) : param.invalid.length > 0 && param.invalid[original],
      );
    }
    if (preserveAliases) {
      expandedAliases.push(original === null ? [] : param.allAliases[original]);
    }
    if (preserveClasses) {
      expandedClasses.push(original === null ? '' : param.equivalenceClasses[original]);
    }
  };
  for (const d of sortedNums) {
    const original = numericValues.get(d) ?? null;
    if (original !== null) {
      expandedValues.push(param.values[original]);
    } else if (config.type === BoundaryType.Integer) {
      expandedValues.push(formatInteger(d));
    } else {
      expandedValues.push(formatFloat(d));
    }
    appendMetadata(original, d);
  }

  // Append non-numeric values at the end.
  for (const original of nonNumericIndices) {
    expandedValues.push(param.values[original]);
    appendMetadata(original, 0);
  }

  const result = new Parameter(param.name, expandedValues);
  if (preserveInvalid) {
    result.setInvalid(expandedInvalid);
  }
  if (preserveAliases) {
    result.setAliases(expandedAliases);
  }
  if (preserveClasses) {
    result.setEquivalenceClasses(expandedClasses);
  }
  return result;
}
