/// @file engine-input.ts
/// @brief The only input the generation engine can be run on.

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from '../model/error.js';
import {
  acceptOptions,
  type GenerateOptions,
  hasBoundaryConfigs,
  optionsParameters,
} from '../model/generate-options.js';
import type { Parameter } from '../model/parameter.js';
import { type TestCase, UNASSIGNED } from '../model/test-case.js';
import { isNumeric, toDouble } from '../util/string_util.js';

/// Validate a seed's row shape against a declared parameter set.
function validateSeedShape(seed: TestCase, params: Parameter[]): string {
  if (seed.values.length !== params.length) {
    return `expected ${params.length} value(s), got ${seed.values.length}`;
  }
  return '';
}

/// Validate one seed index against a declared parameter set.
function validateSeedIndex(seed: TestCase, params: Parameter[], pi: number): string {
  const vi = seed.values[pi];
  if (Number.isInteger(vi) && vi >= 0 && vi < params[pi].size) {
    return '';
  }
  // A row the caller recorded is described back to them in their own terms.
  // The index here is UNASSIGNED whenever the row drifted from the model,
  // and printing that sentinel tells the caller nothing about which part of
  // what they submitted no longer fits.
  const supplied = seed.unresolved?.[pi];
  if (supplied) {
    return `value '${supplied}' is not declared by parameter ${params[pi].name}`;
  }
  if (vi === UNASSIGNED) {
    return `no value recorded for parameter ${params[pi].name}`;
  }
  // Reached only through direct engine use, where the caller supplies
  // indices itself, so the index is the caller's own input.
  return `value index ${vi} is out of range for parameter ${params[pi].name}`;
}

export interface SeedValidationSnapshot {
  error: string;
  parameterIndex: number | null;
}

/// Validate seed shape and indices against the declared value space.
function captureSeedValidation(seed: TestCase, params: Parameter[]): SeedValidationSnapshot {
  const validation: SeedValidationSnapshot = {
    error: validateSeedShape(seed, params),
    parameterIndex: null,
  };
  if (validation.error.length > 0) {
    return validation;
  }
  for (let pi = 0; pi < params.length; ++pi) {
    const indexError = validateSeedIndex(seed, params, pi);
    if (indexError.length > 0) {
      validation.parameterIndex = pi;
      validation.error = indexError;
      return validation;
    }
  }
  return validation;
}

/// Validate that a seed can participate in positive coverage.
export function validatePositiveSeed(
  seed: TestCase,
  params: Parameter[],
  constraints: readonly ConstraintNode[],
  original?: SeedValidationSnapshot,
): string {
  if (original && original.error.length > 0 && original.parameterIndex === null) {
    return original.error;
  }
  const shapeError = validateSeedShape(seed, params);
  if (shapeError.length > 0) {
    return shapeError;
  }

  for (let pi = 0; pi < params.length; ++pi) {
    if (original?.parameterIndex === pi) {
      return original.error;
    }
    const indexError = validateSeedIndex(seed, params, pi);
    if (indexError.length > 0) {
      return indexError;
    }
    const vi = seed.values[pi];
    if (params[pi].isInvalid(vi)) {
      return `value ${params[pi].name}=${params[pi].values[vi]} is marked invalid`;
    }
  }

  for (const constraint of constraints) {
    if (constraint.evaluate(seed.values) !== ConstraintResult.True) {
      return 'violates a constraint';
    }
  }

  return '';
}

/// Move recorded row value indices onto the expanded value space.
///
/// Rows address values by index into the value list the caller declared, while
/// boundary expansion sorts and inserts values, so every index that still names
/// a declared value is looked up again by value identity. An invalid index is
/// left alone for validation to describe; valid positions retain their identity
/// even when another position is invalid. Mirrors RemapSeedValueIndices in the
/// C++ core.
function remapSeedValueIndices(
  declared: Parameter[],
  expanded: Parameter[],
  seeds: TestCase[],
): TestCase[] {
  return seeds.map((seed) => {
    const values = [...seed.values];
    for (let pi = 0; pi < values.length && pi < declared.length; ++pi) {
      const oldIndex = values[pi];
      if (!Number.isInteger(oldIndex) || oldIndex < 0 || oldIndex >= declared[pi].values.length) {
        continue;
      }
      const oldValue = declared[pi].values[oldIndex];
      // Byte equality, not resolveValueName: the name being looked up is the
      // declared value itself, carried across expansion, not text a caller
      // wrote, so the only match that means "this is the same value" is the
      // exact one.
      let newIndex = expanded[pi].findValueIndex(oldValue, true);
      // Numeric identity is decided by the shared decimal grammar, never by
      // Number()'s coercion: coercion also accepts leading whitespace, hex and
      // the empty string, so a row would be remapped here that the C++ core,
      // and every rule written against isNumeric, treats as plain text.
      if (newIndex === UNASSIGNED && isNumeric(oldValue)) {
        const numeric = toDouble(oldValue);
        newIndex = expanded[pi].values.findIndex(
          (candidate) => isNumeric(candidate) && toDouble(candidate) === numeric,
        );
      }
      if (newIndex !== UNASSIGNED && newIndex >= 0) {
        values[pi] = newIndex;
      }
    }
    // The text a position failed to resolve to belongs to the row, not to the
    // value space, so it survives expansion unchanged.
    return { values, unresolved: seed.unresolved };
  });
}

/// Options that reached the engine through the acceptance gate.
///
/// EngineInput.accept is the only thing that can produce one: the constructor is
/// private, and the private field makes the class type nominal, so an object
/// literal cannot stand in for it either. The engine implementation takes this
/// type rather than a GenerateOptions, so an entry point that skips acceptance
/// has nothing to pass it — the omission is a compile error instead of a check
/// that silently does not run. Mirrors core::EngineInput in the C++ core.
export class EngineInput {
  private constructor(
    private readonly accepted: {
      options: GenerateOptions;
      params: Parameter[];
      preservedSeedCount: number;
      originalSeedValidations: SeedValidationSnapshot[];
    },
  ) {}

  /// The accepted options, with boundary parameters already expanded and the
  /// recorded rows remapped onto that value space.
  get options(): GenerateOptions {
    return this.accepted.options;
  }

  /// The expanded parameter set the engine runs on.
  get params(): Parameter[] {
    return this.accepted.params;
  }

  /// How many leading `seeds` rows came from an extend call's `existing`.
  ///
  /// Those rows are reported back even when they no longer fit the model; a row
  /// past this prefix is an ordinary seed and is dropped with a warning.
  get preservedSeedCount(): number {
    return this.accepted.preservedSeedCount;
  }

  /// Shape/index errors against the caller's declared value space, populated
  /// only when boundary expansion ran.
  get originalSeedValidations(): readonly SeedValidationSnapshot[] {
    return this.accepted.originalSeedValidations;
  }

  /// Submit options to the acceptance gate and mint the engine's only input.
  ///
  /// The gate itself is model::acceptOptions, so every rule about what an
  /// options object may contain is stated once and this path adds nothing to
  /// it. Rows are addressed by index into the value list the caller declared, so
  /// what is left here is moving them onto the value space the gate accepted.
  /// Mirrors core::AcceptEngineInput in the C++ core, so both ports answer any
  /// given options with the same code.
  static accept(
    options: GenerateOptions,
    preservedSeedCount: number,
  ): { input: EngineInput | null; error: ErrorInfo } {
    const hasBoundaryExpansion = hasBoundaryConfigs(options);
    const gate = acceptOptions(options);
    if (gate.error.code !== ErrorCode.Ok) {
      return { input: null, error: gate.error };
    }
    const accepted = gate.options;
    // Check caller indices only after the model gate accepts. The gate carries
    // seed rows through unchanged, while its parameter list has expanded.
    const declared =
      hasBoundaryExpansion && options.seeds.length > 0 ? optionsParameters(options) : [];
    const originalSeedValidations = hasBoundaryExpansion
      ? options.seeds.map((seed) => captureSeedValidation(seed, declared))
      : [];
    // Only expansion can move a value's index, so the rows are rebuilt for that
    // case alone; otherwise they already address the value space in hand.
    accepted.seeds = hasBoundaryExpansion
      ? remapSeedValueIndices(declared, gate.params, options.seeds)
      : options.seeds.map((seed) => ({ values: [...seed.values], unresolved: seed.unresolved }));
    return {
      input: new EngineInput({
        options: accepted,
        params: gate.params,
        preservedSeedCount,
        originalSeedValidations,
      }),
      error: okError(),
    };
  }
}
