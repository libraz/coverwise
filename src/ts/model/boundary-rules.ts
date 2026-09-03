/**
 * The boundary acceptance rules — thresholds and rejection wording — held once
 * for every TypeScript surface.
 *
 * Reading a boundary parameter has two steps and each has exactly one owner. A
 * surface converts the caller's `type` / `range` / `step` fields into a
 * BoundaryConfig and may refuse only what it cannot convert, quoting
 * {@link boundaryShapeError}. The model layer then judges the converted config,
 * quoting {@link boundaryAcceptanceError}; every threshold about ordering,
 * finiteness, step size and integer range belongs to that step. A surface that
 * re-derives one of these thresholds accepts or rejects models the rest of the
 * package does not, so the numbers and the text live here and nowhere else.
 *
 * The C++ model layer applies the same rules with the same wording, which
 * `boundary-rules.test.ts` reads out of its source and compares entry by entry.
 */

/**
 * The step an integer expansion takes.
 *
 * Integer expansion always steps by one, so a caller asking for anything else
 * is asking for a value set the engine will not produce.
 */
export const INTEGER_BOUNDARY_STEP = 1;

/**
 * The step a surface records when the caller writes none.
 *
 * A parameter that opts into boundary expansion without naming a step is
 * asking for the default one; every surface substitutes the same number so the
 * config the model layer judges does not depend on which one read the input.
 */
export const DEFAULT_BOUNDARY_STEP = 1;

/**
 * Whether a number may serve as an integer boundary endpoint or declared value.
 *
 * The bound is the exact-integer range of a double, which is what both the
 * generated boundary values and the caller's own value strings have to survive
 * as text without changing identity.
 */
export function isSafeBoundaryInteger(value: number): boolean {
  return Number.isSafeInteger(value);
}

/**
 * Refusals available to a surface converting a caller's boundary fields.
 *
 * A shape refusal says only that the value could not be turned into a
 * BoundaryConfig — never that the config it would have produced is unacceptable.
 */
export const boundaryShapeError = {
  type: (parameterName: string): string =>
    `Invalid boundary type for parameter '${parameterName}'.`,
  range: (parameterName: string): string =>
    `Invalid boundary range for parameter '${parameterName}': expected finite [min, max].`,
  step: (parameterName: string): string =>
    `Invalid boundary step for parameter '${parameterName}': expected a positive finite number.`,
} as const;

/**
 * The per-value metadata a boundary parameter may carry, named as a refusal
 * spells them.
 *
 * Each is an array running parallel to the value list, so expansion can only
 * carry it across when the two have the same length.
 */
export const BOUNDARY_METADATA_FIELDS = ['invalid', 'aliases', 'equivalence classes'] as const;

/** Refusals the model layer raises while judging a converted boundary config. */
export const boundaryAcceptanceError = {
  unknownParameter: (parameterName: string): string =>
    `Unknown parameter in boundary config: ${parameterName}`,
  metadataWithoutValues: (parameterName: string): string =>
    `Metadata requires explicit values for boundary parameter ${parameterName}`,
  metadataLength: (parameterName: string, field: string): string =>
    `Invalid metadata length for parameter '${parameterName}': ${field}`,
  range: (parameterName: string): string =>
    `Boundary range must be finite and ordered for parameter ${parameterName}`,
  expansion: (parameterName: string): string =>
    `Boundary expansion must produce finite values for parameter ${parameterName}`,
  nonFiniteValue: (parameterName: string, value: string): string =>
    `Boundary parameter contains a non-finite numeric value: ${parameterName}=${value}`,
  duplicateIdentities: (parameterName: string): string =>
    `Boundary parameter contains duplicate numeric identities: ${parameterName}`,
  floatStep: (parameterName: string): string =>
    `Boundary step must be finite and positive for parameter ${parameterName}`,
  integerStep: (parameterName: string): string =>
    `Integer boundary step must be ${INTEGER_BOUNDARY_STEP} for parameter ${parameterName}`,
  integerEndpoints: (parameterName: string): string =>
    `Integer boundary endpoints must be safe integers for ${parameterName}`,
  integerValue: (parameterName: string, value: string): string =>
    `Integer boundary parameter contains a non-integral or out-of-range value: ${parameterName}=${value}`,
} as const;
