/// The published input and error vocabularies, in a form a compiler keeps current.
///
/// Each map is keyed by the members of a published shape, so adding a member to
/// that shape — or removing one — stops the object literal below from
/// compiling. The tests that read these maps then have to account for the new
/// member, which is what stops a table of hand-picked cases from falling
/// quietly behind the surface it is supposed to cover.
///
/// The shapes are imported from the entry point rather than from the module
/// that declares them, so what the maps enumerate is what a consumer can see.

import type {
  CoverwiseErrorCode,
  ExtendInput,
  FloatBoundaryParameter,
  GenerateInput,
  IntegerBoundaryParameter,
  ParameterValue,
  PlainParameter,
} from '../../js/index.js';

/// A map that has to name every one of `K`, optional members included.
///
/// `-?` says that being optional on the published shape is no excuse for going
/// unnamed here. Most of what these maps enumerate is optional, so a form that
/// let optionality through would enumerate almost nothing.
export type EveryKey<K extends PropertyKey> = { [P in K]-?: true };

/** Every field of a generate input. */
export const GENERATE_INPUT_FIELDS: EveryKey<keyof GenerateInput> = {
  parameters: true,
  constraints: true,
  strength: true,
  seed: true,
  weights: true,
  seeds: true,
  maxTests: true,
  subModels: true,
};

/**
 * Every field of any published input shape.
 *
 * Keyed by the union rather than by one of the shapes: the extend input is a
 * generate input plus fields of its own, and keying on the generate input alone
 * left those fields named nowhere -- present on the published surface, absent
 * from the vocabulary, and free to be renamed or dropped without anything
 * failing. The union is what makes "a published input field" the thing the
 * compiler counts.
 */
export type PublishedInputField = keyof GenerateInput | keyof ExtendInput;

export const PUBLISHED_INPUT_FIELDS: EveryKey<PublishedInputField> = {
  ...GENERATE_INPUT_FIELDS,
  mode: true,
};

/** Every field any published parameter shape declares. */
export type ParameterField =
  | keyof PlainParameter
  | keyof IntegerBoundaryParameter
  | keyof FloatBoundaryParameter;

export const PARAMETER_FIELDS: EveryKey<ParameterField> = {
  name: true,
  values: true,
  type: true,
  range: true,
  step: true,
};

/** Every field of the object form of a parameter value. */
export const PARAMETER_VALUE_FIELDS: EveryKey<keyof ParameterValue> = {
  value: true,
  invalid: true,
  aliases: true,
  class: true,
};

/** Every code a caller is documented to be able to branch on. */
export const ERROR_CODES: EveryKey<CoverwiseErrorCode> = {
  CONSTRAINT_ERROR: true,
  INSUFFICIENT_COVERAGE: true,
  INVALID_INPUT: true,
  TUPLE_EXPLOSION: true,
};
