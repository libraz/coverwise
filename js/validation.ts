/**
 * Shared input-validation helpers for the coverwise public surfaces.
 *
 * Both the WASM-backed (`js/index.ts`) and pure-TypeScript (`js/pure/index.ts`)
 * entry points run identical structural checks before delegating to their
 * respective engines. This module is the single source of truth for that logic
 * so the two surfaces cannot drift apart.
 *
 * Every public surface throws {@link CoverwiseError}. The injected factory is
 * retained for direct helper tests and embedders, while the package entrypoints
 * both provide the canonical CoverwiseError factory.
 */

import type { GenerateInput } from './types.js';
import { CoverwiseError } from './types.js';

/**
 * Factory for the error thrown by the numeric scalar validators. The WASM
 * surface passes a plain-`Error` factory; the pure surface passes one that
 * builds a {@link CoverwiseError}. Preserves each surface's historical behavior.
 */
export type ScalarErrorFactory = (message: string) => Error;

/**
 * Validate the optional `strength` field.
 *
 * @param strength - Candidate strength; `undefined`/`null` default to 2.
 * @param makeError - Error factory for the invalid case.
 * @returns The validated strength.
 */
export function validateStrength(strength: unknown, makeError: ScalarErrorFactory): number {
  if (strength === undefined || strength === null) {
    return 2;
  }
  if (typeof strength !== 'number' || !Number.isInteger(strength) || strength <= 0) {
    throw makeError(`Invalid strength: ${String(strength)}. Must be a positive integer.`);
  }
  return strength;
}

/**
 * Validate the optional `maxTests` field.
 *
 * @param maxTests - Candidate value; `undefined`/`null` are accepted.
 * @param makeError - Error factory for the invalid case.
 */
export function validateMaxTests(maxTests: unknown, makeError: ScalarErrorFactory): void {
  if (maxTests === undefined || maxTests === null) {
    return;
  }
  if (
    typeof maxTests !== 'number' ||
    !Number.isInteger(maxTests) ||
    maxTests < 0 ||
    maxTests > 0xffffffff
  ) {
    throw makeError(
      `Invalid maxTests: ${String(maxTests)}. Must be an integer in [0, 4294967295].`,
    );
  }
}

/**
 * Validate the optional `seed` field.
 *
 * @param seed - Candidate seed; `undefined`/`null` are accepted.
 * @param makeError - Error factory for the invalid case.
 */
export function validateSeed(seed: unknown, makeError: ScalarErrorFactory): void {
  if (seed === undefined || seed === null) {
    return;
  }
  if (typeof seed !== 'number' || !Number.isInteger(seed) || seed < 0 || seed > 0xffffffff) {
    throw makeError(`Invalid seed: ${String(seed)}. Must be an integer in [0, 4294967295].`);
  }
}

function invalid(message: string): never {
  throw new CoverwiseError('INVALID_INPUT', message);
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value);
}

function isParameterScalar(value: unknown): value is string | number | boolean {
  return (
    typeof value === 'string' ||
    typeof value === 'boolean' ||
    (typeof value === 'number' && Number.isFinite(value))
  );
}

const DECIMAL_RE = /^[+-]?(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?$/;
const MAX_PARAMETERS = 1_024;
const MAX_VALUES_PER_PARAMETER = 16_384;
const MAX_TESTS = 100_000;
const MAX_CONSTRAINTS = 256;
const MAX_STRING_BYTES = 64 * 1024;
const MAX_AGGREGATE_STRING_BYTES = 1 * 1024 * 1024;

function utf8Bytes(value: string): number {
  return new TextEncoder().encode(value).byteLength;
}

function validateStringBudget(value: string, context: string, aggregate: { value: number }): void {
  const bytes = utf8Bytes(value);
  if (bytes > MAX_STRING_BYTES) {
    invalid(`${context} exceeds ${MAX_STRING_BYTES} UTF-8 bytes.`);
  }
  aggregate.value += bytes;
  if (aggregate.value > MAX_AGGREGATE_STRING_BYTES) {
    invalid(`Input strings exceed ${MAX_AGGREGATE_STRING_BYTES} UTF-8 bytes.`);
  }
}

function validateAggregateStringBudget(value: unknown): void {
  const seen = new Set<object>();
  let bytes = 0;
  const visit = (current: unknown): void => {
    if (typeof current === 'string') {
      bytes += utf8Bytes(current);
      if (bytes > MAX_AGGREGATE_STRING_BYTES) {
        invalid(`Input strings exceed ${MAX_AGGREGATE_STRING_BYTES} UTF-8 bytes.`);
      }
      return;
    }
    if (typeof current !== 'object' || current === null || seen.has(current)) {
      return;
    }
    seen.add(current);
    for (const nested of Array.isArray(current) ? current : Object.values(current)) {
      visit(nested);
    }
  };
  visit(value);
}

function validateBoundaryValue(
  value: unknown,
  parameter: Record<string, unknown>,
  name: string,
): void {
  const scalar = isRecord(value) && Object.hasOwn(value, 'value') ? value.value : value;
  if (typeof scalar !== 'number' && typeof scalar !== 'string') {
    return;
  }
  if (typeof scalar === 'string' && !DECIMAL_RE.test(scalar)) {
    return;
  }
  const numeric = typeof scalar === 'number' ? scalar : Number(scalar);
  if (!Number.isFinite(numeric)) {
    invalid(`Boundary parameter '${name}' contains a non-finite numeric value.`);
  }
  if (parameter.type === 'integer' && !Number.isSafeInteger(numeric)) {
    invalid(`Integer boundary parameter '${name}' contains a non-integral value.`);
  }
}

function validateBoundary(parameter: Record<string, unknown>, name: string): void {
  const hasBoundary =
    Object.hasOwn(parameter, 'type') ||
    Object.hasOwn(parameter, 'range') ||
    Object.hasOwn(parameter, 'step');
  if (!hasBoundary) {
    return;
  }

  if (parameter.type !== 'integer' && parameter.type !== 'float') {
    invalid(`Invalid boundary type for parameter '${name}'.`);
  }
  const range = parameter.range;
  if (
    !Array.isArray(range) ||
    range.length !== 2 ||
    typeof range[0] !== 'number' ||
    typeof range[1] !== 'number' ||
    !Number.isFinite(range[0]) ||
    !Number.isFinite(range[1]) ||
    range[0] > range[1]
  ) {
    invalid(`Invalid boundary range for parameter '${name}': expected finite [min, max].`);
  }
  if (parameter.type === 'integer') {
    if (
      !Number.isSafeInteger(range[0]) ||
      !Number.isSafeInteger(range[1]) ||
      range[0] <= Number.MIN_SAFE_INTEGER ||
      range[1] >= Number.MAX_SAFE_INTEGER
    ) {
      invalid(`Invalid integer boundary range for parameter '${name}'.`);
    }
    if (parameter.step !== undefined && parameter.step !== 1) {
      invalid(`Integer boundary step for parameter '${name}' must be 1 when provided.`);
    }
  } else {
    const step = parameter.step ?? 1;
    if (typeof step !== 'number' || !Number.isFinite(step) || step <= 0) {
      invalid(`Invalid boundary step for parameter '${name}': expected a positive finite number.`);
    }
    if (!Number.isFinite(range[0] - step) || !Number.isFinite(range[1] + step)) {
      invalid(`Boundary expansion for parameter '${name}' produces a non-finite value.`);
    }
  }
}

/** Validate the complete runtime shape of a parameter array. */
export function validateParameters(parameters: unknown): void {
  if (!Array.isArray(parameters)) {
    invalid('Invalid parameters: must be an array.');
  }
  if (parameters.length > MAX_PARAMETERS) {
    invalid(`Invalid parameters: maximum is ${MAX_PARAMETERS}.`);
  }
  const foldedNames = new Set<string>();
  const aggregate = { value: 0 };
  for (let pi = 0; pi < parameters.length; ++pi) {
    const parameter = parameters[pi];
    if (!isRecord(parameter)) {
      invalid(`Invalid parameter at index ${pi}: must be an object.`);
    }
    if (typeof parameter.name !== 'string' || parameter.name.length === 0) {
      invalid('Parameter name must be a non-empty string');
    }
    validateStringBudget(parameter.name, `Parameter name '${parameter.name}'`, aggregate);
    const foldedName = parameter.name.replace(/[A-Z]/g, (char) => char.toLowerCase());
    if (foldedNames.has(foldedName)) {
      invalid(`Parameter names must not differ only by ASCII case: '${parameter.name}'`);
    }
    foldedNames.add(foldedName);
    if (!Array.isArray(parameter.values)) {
      invalid(`Parameter '${parameter.name}' must have at least one value`);
    }
    if (parameter.values.length > MAX_VALUES_PER_PARAMETER) {
      invalid(
        `Parameter '${parameter.name}' has too many values (maximum ${MAX_VALUES_PER_PARAMETER}).`,
      );
    }
    validateBoundary(parameter, parameter.name);
    if (parameter.values.length === 0 && parameter.type === undefined) {
      invalid(`Parameter '${parameter.name}' must have at least one value`);
    }
    const boundaryNumericIdentities = new Set<number>();
    for (let vi = 0; vi < parameter.values.length; ++vi) {
      const value = parameter.values[vi];
      if (parameter.type !== undefined) {
        validateBoundaryValue(value, parameter, parameter.name);
        const scalar = isRecord(value) && Object.hasOwn(value, 'value') ? value.value : value;
        if (
          (typeof scalar === 'number' || (typeof scalar === 'string' && DECIMAL_RE.test(scalar))) &&
          Number.isFinite(Number(scalar))
        ) {
          const numeric = Number(scalar);
          if (boundaryNumericIdentities.has(numeric)) {
            invalid(`Boundary parameter '${parameter.name}' has duplicate numeric identities.`);
          }
          boundaryNumericIdentities.add(numeric);
        }
      }
      if (isParameterScalar(value)) {
        if (typeof value === 'string') {
          validateStringBudget(value, `${parameter.name}[${vi}]`, aggregate);
        }
        continue;
      }
      if (typeof value.value === 'string') {
        validateStringBudget(value.value, `${parameter.name}[${vi}]`, aggregate);
      }
      if (!isRecord(value) || !Object.hasOwn(value, 'value') || !isParameterScalar(value.value)) {
        invalid(`Invalid value at ${parameter.name}[${vi}]: expected string, number, or boolean.`);
      }
      if (value.invalid !== undefined && typeof value.invalid !== 'boolean') {
        invalid(`Invalid flag at ${parameter.name}[${vi}] must be boolean.`);
      }
      if (
        value.aliases !== undefined &&
        (!Array.isArray(value.aliases) ||
          value.aliases.some((alias) => typeof alias !== 'string' || alias.length === 0))
      ) {
        invalid(`Aliases at ${parameter.name}[${vi}] must be non-empty strings.`);
      }
      for (const alias of value.aliases ?? []) {
        validateStringBudget(alias, `Alias at ${parameter.name}[${vi}]`, aggregate);
      }
      if (value.class !== undefined && typeof value.class !== 'string') {
        invalid(`Class at ${parameter.name}[${vi}] must be a string.`);
      }
      if (typeof value.class === 'string') {
        validateStringBudget(value.class, `Class at ${parameter.name}[${vi}]`, aggregate);
      }
    }
  }
}

/** Validate that a test-case array contains object rows with scalar values. */
export function validateTestArray(tests: unknown, field: string): void {
  if (!Array.isArray(tests)) {
    invalid(`Invalid ${field}: must be an array.`);
  }
  if (tests.length > MAX_TESTS) {
    invalid(`Invalid ${field}: maximum is ${MAX_TESTS} rows.`);
  }
  for (let i = 0; i < tests.length; ++i) {
    const test = tests[i];
    if (!isRecord(test)) {
      invalid(`Invalid ${field}[${i}]: must be an object.`);
    }
    if (Object.keys(test).length > MAX_PARAMETERS) {
      invalid(`Invalid ${field}[${i}]: too many fields.`);
    }
    for (const [name, value] of Object.entries(test)) {
      if (!isParameterScalar(value)) {
        invalid(`Invalid ${field}[${i}].${name}: expected string, number, or boolean.`);
      }
      if (typeof value === 'string' && utf8Bytes(value) > MAX_STRING_BYTES) {
        invalid(`Invalid ${field}[${i}].${name}: string exceeds ${MAX_STRING_BYTES} UTF-8 bytes.`);
      }
    }
  }
}

/** Validate an optional constraint expression array. */
export function validateConstraints(constraints: unknown): void {
  if (constraints === undefined || constraints === null) {
    return;
  }
  if (!Array.isArray(constraints) || constraints.some((expr) => typeof expr !== 'string')) {
    invalid('Invalid constraints: must be an array of strings.');
  }
  if (constraints.length > MAX_CONSTRAINTS) {
    invalid(`Invalid constraints: maximum is ${MAX_CONSTRAINTS}.`);
  }
  for (const expression of constraints) {
    if (utf8Bytes(expression) > MAX_STRING_BYTES) {
      invalid(`Invalid constraint: maximum expression size is ${MAX_STRING_BYTES} UTF-8 bytes.`);
    }
  }
}

function validateSeedRows(input: Record<string, unknown>): void {
  if (input.seeds === undefined || input.seeds === null) {
    return;
  }
  validateTestArray(input.seeds, 'seeds');
  const seeds = input.seeds as Array<Record<string, unknown>>;
  const parameters = input.parameters as Array<Record<string, unknown>>;
  const names = new Set(parameters.map((parameter) => parameter.name as string));
  for (let i = 0; i < seeds.length; ++i) {
    const seed = seeds[i];
    for (const name of names) {
      if (!Object.hasOwn(seed, name)) {
        invalid(`Invalid seeds[${i}]: missing parameter '${name}'.`);
      }
    }
    for (const name of Object.keys(seed)) {
      if (!names.has(name)) {
        invalid(`Invalid seeds[${i}]: unknown parameter '${name}'.`);
      }
    }
  }
}

function validateSubModels(subModels: unknown): void {
  if (subModels === undefined || subModels === null) {
    return;
  }
  if (!Array.isArray(subModels)) {
    invalid('Invalid subModels: must be an array.');
  }
  for (let i = 0; i < subModels.length; ++i) {
    const subModel = subModels[i];
    if (
      !isRecord(subModel) ||
      !Array.isArray(subModel.parameters) ||
      subModel.parameters.length === 0 ||
      subModel.parameters.some((name) => typeof name !== 'string') ||
      typeof subModel.strength !== 'number' ||
      !Number.isInteger(subModel.strength) ||
      subModel.strength < 1
    ) {
      invalid(`Invalid subModels[${i}].`);
    }
  }
}

function validateWeights(weights: unknown): void {
  if (weights === undefined || weights === null) {
    return;
  }
  if (!isRecord(weights)) {
    invalid('Invalid weights: must be an object.');
  }
  for (const [parameter, valueWeights] of Object.entries(weights)) {
    if (!isRecord(valueWeights)) {
      invalid(`Invalid weights.${parameter}: must be an object.`);
    }
    for (const [value, weight] of Object.entries(valueWeights)) {
      if (typeof weight !== 'number' || !Number.isFinite(weight) || weight <= 0) {
        invalid(`Invalid weight for ${parameter}=${value}: must be finite and positive.`);
      }
    }
  }
}

/**
 * Run the full structural validation for a {@link GenerateInput}.
 *
 * @param input - The generate/extend input to validate.
 * @param makeError - Error factory for the numeric scalar checks.
 */
export function validateGenerateInput(input: GenerateInput, makeError: ScalarErrorFactory): void {
  if (!isRecord(input)) {
    invalid('Invalid input: must be an object.');
  }
  validateAggregateStringBudget(input);
  validateParameters(input.parameters);
  validateStrength(input.strength, makeError);
  validateMaxTests(input.maxTests, makeError);
  validateSeed(input.seed, makeError);
  validateConstraints(input.constraints);
  validateSeedRows(input);
  validateSubModels(input.subModels);
  validateWeights(input.weights);
}

/** The extend modes currently supported by the engine. */
const SUPPORTED_EXTEND_MODES = ['strict'] as const;

/**
 * Validate the optional `mode` field of an extend input. `'strict'` (keep
 * existing tests as-is) is the only supported mode today; any other value is
 * rejected rather than silently ignored. Throws {@link CoverwiseError} otherwise.
 */
export function validateExtendMode(mode: unknown): void {
  if (mode === undefined || mode === null) {
    return;
  }
  if (typeof mode !== 'string' || !(SUPPORTED_EXTEND_MODES as readonly string[]).includes(mode)) {
    throw new CoverwiseError(
      'INVALID_INPUT',
      `Invalid extend mode: ${String(mode)}. Supported modes: ${SUPPORTED_EXTEND_MODES.join(', ')}.`,
    );
  }
}
