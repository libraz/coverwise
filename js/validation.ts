/**
 * Shared input-validation helpers for the coverwise public surfaces.
 *
 * Both the WASM-backed (`js/index.ts`) and pure-TypeScript (`js/pure/index.ts`)
 * entry points run identical structural checks before delegating to their
 * respective engines. This module is the single source of truth for that logic
 * so the two surfaces cannot drift apart.
 *
 * The only intentional difference between the surfaces is the error type thrown
 * for the numeric scalar checks (strength / maxTests / seed): the WASM surface
 * throws a plain `Error` while the pure surface throws a {@link CoverwiseError}.
 * That choice is injected via {@link ScalarErrorFactory}; the array/parameter
 * checks always throw a `CoverwiseError`, matching both surfaces today.
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
  if (typeof maxTests !== 'number' || !Number.isInteger(maxTests) || maxTests < 0) {
    throw makeError(`Invalid maxTests: ${String(maxTests)}. Must be a non-negative integer.`);
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

/** Validate that `parameters` is an array. Throws {@link CoverwiseError} otherwise. */
export function validateParameters(parameters: unknown): void {
  if (!Array.isArray(parameters)) {
    throw new CoverwiseError('INVALID_INPUT', 'Invalid parameters: must be an array.');
  }
}

/** Validate that a test-case array argument is an array. Throws {@link CoverwiseError} otherwise. */
export function validateTestArray(tests: unknown, field: string): void {
  if (!Array.isArray(tests)) {
    throw new CoverwiseError('INVALID_INPUT', `Invalid ${field}: must be an array.`);
  }
}

/**
 * Run the full structural validation for a {@link GenerateInput}.
 *
 * @param input - The generate/extend input to validate.
 * @param makeError - Error factory for the numeric scalar checks.
 */
export function validateGenerateInput(input: GenerateInput, makeError: ScalarErrorFactory): void {
  validateParameters(input.parameters);
  validateStrength(input.strength, makeError);
  validateMaxTests(input.maxTests, makeError);
  validateSeed(input.seed, makeError);
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
