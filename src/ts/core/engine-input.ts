/// @file engine-input.ts
/// @brief The only input the generation engine can be run on.

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from '../model/error.js';
import {
  acceptOptions,
  type ChargedText,
  type GenerateOptions,
  hasBoundaryConfigs,
  optionsParameters,
} from '../model/generate-options.js';
import { MAX_TESTS } from '../model/limits.js';
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
  if (supplied !== undefined) {
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
function validatePositiveSeed(
  seed: TestCase,
  params: Parameter[],
  constraints: readonly ConstraintNode[],
  original: SeedValidationSnapshot,
): string {
  if (original.error.length > 0 && original.parameterIndex === null) {
    return original.error;
  }
  const shapeError = validateSeedShape(seed, params);
  if (shapeError.length > 0) {
    return shapeError;
  }

  for (let pi = 0; pi < params.length; ++pi) {
    if (original.parameterIndex === pi) {
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

/// Whether a row has exactly one invalid value and otherwise fits the model.
function isSingleFaultRow(
  row: SeedRow,
  params: Parameter[],
  constraints: readonly ConstraintNode[],
): boolean {
  if (row.original.error.length > 0 || row.test.values.length !== params.length) {
    return false;
  }
  let faults = 0;
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = row.test.values[pi];
    if (!Number.isInteger(vi) || vi < 0 || vi >= params[pi].size) {
      return false;
    }
    if (params[pi].isInvalid(vi)) {
      ++faults;
    }
  }
  if (faults !== 1) {
    return false;
  }
  return constraints.every(
    (constraint) => constraint.evaluate(row.test.values) === ConstraintResult.True,
  );
}

/// One row the engine is seeded with, in the order it is reported back.
///
/// Extend's `existing` rows come first and are kept verbatim even when they no
/// longer fit the model; the caller's `seeds` follow and are dropped with a
/// warning when they do not. Mirrors core::SeedRow in the C++ core.
export interface SeedRow {
  /// The row, with its indices moved onto the accepted value space.
  test: TestCase;
  /// Whether the row came from extend's `existing`.
  preserved: boolean;
  /// Position in the caller's own array, for diagnostics.
  sourceIndex: number;
  /// Shape/index error against the caller's declared value space, populated
  /// only when boundary expansion ran.
  original: SeedValidationSnapshot;
}

/// What part a seed row can play in coverage.
export interface SeedClassification {
  /// Why the row cannot count toward positive coverage; empty when it can.
  error: string;
  /// A preserved row with exactly one invalid value that otherwise satisfies
  /// the model, which counts toward negative coverage instead.
  singleFault: boolean;
}

/// Classify a seed row against the accepted parameters and constraints.
export function classifySeedRow(
  row: SeedRow,
  params: Parameter[],
  constraints: readonly ConstraintNode[],
): SeedClassification {
  const error = validatePositiveSeed(row.test, params, constraints, row.original);
  // Only a preserved row is reported back, so only it can stand for a negative case.
  const singleFault =
    error.length > 0 && row.preserved && isSingleFaultRow(row, params, constraints);
  return { error, singleFault };
}

/// Move recorded row value indices onto the expanded value space.
///
/// Rows address values by index into the value list the caller declared, while
/// boundary expansion sorts and inserts values, so every index that still names
/// a declared value is looked up again by value identity. An index that names
/// no declared value becomes UNASSIGNED, so it cannot read as a value expansion
/// inserted; validation describes it from the declared snapshot. Mirrors
/// RemapSeedValueIndices in the C++ core.
function remapSeedValueIndices(
  declared: Parameter[],
  expanded: Parameter[],
  rows: SeedRow[],
): void {
  for (const row of rows) {
    const values = row.test.values;
    for (let pi = 0; pi < values.length && pi < declared.length; ++pi) {
      const oldIndex = values[pi];
      if (!Number.isInteger(oldIndex) || oldIndex < 0 || oldIndex >= declared[pi].values.length) {
        values[pi] = UNASSIGNED;
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
        const found = expanded[pi].values.findIndex(
          (candidate) => isNumeric(candidate) && toDouble(candidate) === numeric,
        );
        newIndex = found >= 0 ? found : UNASSIGNED;
      }
      values[pi] = newIndex;
    }
  }
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
      seedRows: SeedRow[];
    },
  ) {}

  /// The accepted options, with boundary parameters already expanded. `seeds`
  /// is empty: the rows have moved to seedRows.
  get options(): GenerateOptions {
    return this.accepted.options;
  }

  /// The expanded parameter set the engine runs on.
  get params(): Parameter[] {
    return this.accepted.params;
  }

  /// Extend's `existing` rows followed by the caller's `seeds`.
  get seedRows(): readonly SeedRow[] {
    return this.accepted.seedRows;
  }

  /// Submit options to the acceptance gate and mint the engine's only input.
  ///
  /// The gate itself is model::acceptOptions, so every rule about what an
  /// options object may contain is stated once. This is also the only place
  /// extend's `existing` rows enter the engine: they are held to maxTests and
  /// to the row limit together with `seeds`, charged by the gate under the same
  /// rules as `seeds`, and remapped by value identity onto the expanded value
  /// space the gate accepted, as every seed row is. `charged` is what the
  /// calling surface already counted, handed to the gate unchanged. Mirrors
  /// core::AcceptEngineInput in the C++ core, so both ports answer any given
  /// options with the same code.
  static accept(
    options: GenerateOptions,
    existing: readonly TestCase[],
    charged: ChargedText,
  ): { input: EngineInput | null; error: ErrorInfo } {
    let submitted = options;
    if (existing.length > 0) {
      if (options.maxTests > 0 && existing.length > options.maxTests) {
        return {
          input: null,
          error: {
            code: ErrorCode.InvalidInput,
            message: 'maxTests cannot be smaller than the existing test count',
            detail: `maxTests=${options.maxTests}, existing=${existing.length}`,
          },
        };
      }
      // Both arrays become one suite, so they share one row limit, and the
      // refusal names the pair rather than a seed count the caller never wrote.
      const rowCount = existing.length + options.seeds.length;
      if (rowCount > MAX_TESTS) {
        return {
          input: null,
          error: {
            code: ErrorCode.InvalidInput,
            message: `Existing and seed test count ${rowCount} exceeds maximum of ${MAX_TESTS}`,
            detail: '',
          },
        };
      }
      // The gate charges the rows it is shown, so `existing` is shown to it as
      // leading seeds and split off again below.
      submitted = { ...options, seeds: [...existing, ...options.seeds] };
    }
    const remap = hasBoundaryConfigs(submitted) && submitted.seeds.length > 0;
    const gate = acceptOptions(submitted, charged);
    if (gate.error.code !== ErrorCode.Ok) {
      return { input: null, error: gate.error };
    }
    const declared = remap ? optionsParameters(submitted) : [];
    const seedRows = submitted.seeds.map((seed, si): SeedRow => {
      const preserved = si < existing.length;
      return {
        test: { values: [...seed.values], unresolved: seed.unresolved },
        preserved,
        sourceIndex: preserved ? si : si - existing.length,
        original: remap
          ? captureSeedValidation(seed, declared)
          : { error: '', parameterIndex: null },
      };
    });
    if (remap) {
      remapSeedValueIndices(declared, gate.params, seedRows);
    }
    return {
      input: new EngineInput({
        options: { ...gate.options, seeds: [] },
        params: gate.params,
        seedRows,
      }),
      error: okError(),
    };
  }
}
