/// Input/output data structures for test generation.

import { compareUtf8, isNumeric } from '../util/string_util.js';
import type { BoundaryConfig } from './boundary.js';
import { BoundaryType, expandBoundaryValues } from './boundary.js';
import {
  BOUNDARY_METADATA_FIELDS,
  boundaryAcceptanceError,
  INTEGER_BOUNDARY_STEP,
  isSafeBoundaryInteger,
} from './boundary-rules.js';
import { aggregateBudgetExceeded, chargedStringContext, stringBudgetExceeded } from './budget.js';
import { ErrorCode, type ErrorInfo, okError } from './error.js';
import {
  MAX_AGGREGATE_STRING_BYTES,
  MAX_CONSTRAINTS,
  MAX_STRING_BYTES,
  MAX_TESTS,
} from './limits.js';
import { Parameter, resolveValueName, UNASSIGNED, validateParameters } from './parameter.js';
import type { TestCase } from './test-case.js';
import { utf8ByteLength } from './utf8.js';

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
    boundaryConfigs:
      params?.boundaryConfigs ?? (Object.create(null) as Record<string, BoundaryConfig>),
  };
}

/**
 * Look up a parameter's boundary config as an own property.
 *
 * `boundaryConfigs` may be a caller-supplied plain object, so a bare index would
 * resolve inherited `Object.prototype` members and mistake a parameter named
 * `constructor` / `toString` / `valueOf` for a boundary parameter.
 */
export function getBoundaryConfig(
  options: GenerateOptions,
  paramName: string,
): BoundaryConfig | undefined {
  return Object.hasOwn(options.boundaryConfigs, paramName)
    ? options.boundaryConfigs[paramName]
    : undefined;
}

/** Whether any parameter opts into boundary expansion. */
export function hasBoundaryConfigs(options: GenerateOptions): boolean {
  return Object.keys(options.boundaryConfigs).length > 0;
}

function invalid(message: string, detail = ''): ErrorInfo {
  return { code: ErrorCode.InvalidInput, message, detail };
}

/**
 * Charge the model's own strings against the documented byte budgets.
 *
 * Both the per-string and the aggregate bound are documented input limits, so
 * they belong to the acceptance contract rather than to any one surface's
 * reader. Mirrors ValidateStringBudget in the C++ model layer.
 *
 * The walk covers every kind of the charged set, row values included. This
 * entry is the one no reader stands in front of — a caller reaching the engine
 * directly hands over a `GenerateOptions` and nothing upstream has counted
 * anything — which is the regime the C++ gate is in when it is given
 * `ChargedText::None()`. A row enters the engine as value indices, so the only
 * row text here is that of a position which did not resolve, kept so the
 * diagnostics can quote it back; leaving it uncharged would put this path
 * outside the published budget entirely.
 *
 * The package entry points charge the caller's rows themselves before the
 * engine is reached. That is not a second charge against the same allowance:
 * their total is a separate accumulator judged against the same limit, and the
 * strings this walk sees are a subset of the ones they saw, so this check
 * cannot refuse an input they let through.
 */
function validateStringBudget(options: GenerateOptions, params: Parameter[]): ErrorInfo {
  let aggregate = 0;
  const account = (value: string, context: () => string): ErrorInfo => {
    const bytes = utf8ByteLength(value);
    if (bytes > MAX_STRING_BYTES) {
      return invalid(stringBudgetExceeded(context()));
    }
    aggregate += bytes;
    if (aggregate > MAX_AGGREGATE_STRING_BYTES) {
      return invalid(aggregateBudgetExceeded());
    }
    return okError();
  };

  for (const param of params) {
    let error = account(param.name, () => chargedStringContext.parameterName(param.name));
    if (error.code !== ErrorCode.Ok) {
      return error;
    }
    for (let index = 0; index < param.values.length; ++index) {
      error = account(param.values[index], () =>
        chargedStringContext.parameterValue(param.name, index),
      );
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
    // Both metadata lists run parallel to the value list, so a position in them
    // is the value index a refusal names.
    for (let index = 0; index < param.allAliases.length; ++index) {
      for (const alias of param.allAliases[index]) {
        error = account(alias, () => chargedStringContext.valueAlias(param.name, index));
        if (error.code !== ErrorCode.Ok) {
          return error;
        }
      }
    }
    for (let index = 0; index < param.equivalenceClasses.length; ++index) {
      error = account(param.equivalenceClasses[index], () =>
        chargedStringContext.equivalenceClass(param.name, index),
      );
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  for (const expression of options.constraintExpressions) {
    const error = account(expression, chargedStringContext.constraintExpression);
    if (error.code !== ErrorCode.Ok) {
      return error;
    }
  }
  for (const subModel of options.subModels) {
    for (const name of subModel.parameterNames) {
      const error = account(name, chargedStringContext.subModelParameterName);
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  for (const [paramName, valueWeights] of Object.entries(options.weights.entries)) {
    let error = account(paramName, chargedStringContext.weightParameterName);
    if (error.code !== ErrorCode.Ok) {
      return error;
    }
    for (const valueName of Object.keys(valueWeights)) {
      error = account(valueName, chargedStringContext.weightValueName);
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  for (let row = 0; row < options.seeds.length; ++row) {
    const unresolved = options.seeds[row].unresolved;
    if (!unresolved) {
      continue;
    }
    for (const text of unresolved) {
      const error = account(text, () => chargedStringContext.rowValue('seeds', row));
      if (error.code !== ErrorCode.Ok) {
        return error;
      }
    }
  }
  return okError();
}

/**
 * Validate every boundary config against the declared value list.
 *
 * This is the only place a boundary config is judged: a surface converts the
 * caller's fields into one and this decides whether the engine will honor it.
 * Runs before expansion because the checks are about the configured range and
 * the values the caller wrote down — after expansion the generated boundary
 * values would mask, for instance, a duplicate numeric identity.
 */
function validateBoundaryConfigs(
  boundaryConfigs: Record<string, BoundaryConfig>,
  params: Parameter[],
): ErrorInfo {
  const byName = new Map(params.map((param) => [param.name, param]));
  // Walked in the core's own key order rather than the order the caller wrote
  // the object in, so a model with more than one malformed boundary config is
  // refused over the same parameter on every surface. The core holds these as
  // std::map, and a JavaScript object holds them in insertion order.
  for (const paramName of Object.keys(boundaryConfigs).sort(compareUtf8)) {
    const config = boundaryConfigs[paramName];
    const param = byName.get(paramName);
    if (!param) {
      return invalid(boundaryAcceptanceError.unknownParameter(paramName));
    }
    if (
      param.values.length === 0 &&
      (param.invalid.length > 0 ||
        param.allAliases.length > 0 ||
        param.equivalenceClasses.length > 0)
    ) {
      return invalid(boundaryAcceptanceError.metadataWithoutValues(paramName));
    }
    if (
      !Number.isFinite(config.minValue) ||
      !Number.isFinite(config.maxValue) ||
      config.minValue > config.maxValue
    ) {
      return invalid(boundaryAcceptanceError.range(paramName));
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
      return invalid(boundaryAcceptanceError.expansion(paramName));
    }
    const numericIdentities = new Set<number>();
    for (const value of param.values) {
      if (!isNumeric(value)) {
        continue;
      }
      const numeric = Number(value);
      if (!Number.isFinite(numeric)) {
        return invalid(boundaryAcceptanceError.nonFiniteValue(paramName, value));
      }
      if (numericIdentities.has(numeric)) {
        return invalid(boundaryAcceptanceError.duplicateIdentities(paramName));
      }
      numericIdentities.add(numeric);
    }
    if (config.type === BoundaryType.Float) {
      if (!Number.isFinite(config.step) || config.step <= 0) {
        return invalid(boundaryAcceptanceError.floatStep(paramName));
      }
    } else {
      // Integer expansion always steps by one, so a caller asking for anything
      // else is asking for a value set the engine will not produce. Rejecting is
      // the only answer that keeps the model JSON meaning one thing everywhere.
      if (config.step !== INTEGER_BOUNDARY_STEP) {
        return invalid(boundaryAcceptanceError.integerStep(paramName));
      }
      if (!isSafeBoundaryInteger(config.minValue) || !isSafeBoundaryInteger(config.maxValue)) {
        return invalid(boundaryAcceptanceError.integerEndpoints(paramName));
      }
      // Gate on the shared isNumeric predicate (same as the identity loop above
      // and the C++ core) so both surfaces classify a value as numeric — and
      // therefore range-check it — identically.
      for (const value of param.values) {
        if (!isNumeric(value)) {
          continue;
        }
        if (!isSafeBoundaryInteger(Number(value))) {
          return invalid(boundaryAcceptanceError.integerValue(paramName, value));
        }
      }
    }
  }
  return okError();
}

/**
 * Refuse per-value metadata that does not run parallel to the value list.
 *
 * Expansion rebuilds these arrays against the value set it produces, so a
 * length that never matched is silently made to match and the flags land on
 * whichever values happened to be generated. The C++ core refuses inside
 * ExpandBoundaryValues; the check sits here because the TypeScript expansion
 * has no error channel of its own, and putting it in a caller instead would be
 * a model rule stated outside the model.
 */
function validateBoundaryMetadata(param: Parameter): ErrorInfo {
  const [invalidField, aliasesField, classesField] = BOUNDARY_METADATA_FIELDS;
  const lengths: Array<[string, number]> = [
    [invalidField, param.invalid.length],
    [aliasesField, param.allAliases.length],
    [classesField, param.equivalenceClasses.length],
  ];
  for (const [field, length] of lengths) {
    if (length > 0 && length !== param.values.length) {
      return invalid(boundaryAcceptanceError.metadataLength(param.name, field));
    }
  }
  return okError();
}

/**
 * Validate every boundary config and expand the parameters it covers.
 *
 * Surfaces call this before resolving `seeds` / `existing` rows, which need the
 * final value list to map a value name to an index, and before judging the
 * parameter set — the rules apply to the value space the engine will use, not to
 * the shorter list the caller wrote down. Mirrors ExpandBoundaries in the C++
 * model layer, including the order it applies its two rules in: every config is
 * judged before any parameter is expanded, so which of two malformed
 * parameters is named does not depend on where each sits in the list.
 *
 * @param params - Parameters to expand; not modified.
 * @param boundaryConfigs - Config per parameter name.
 * @returns The expanded parameters, and an error describing the first malformed
 *   config (in which case the parameters are returned unchanged).
 */
export function expandBoundaries(
  params: Parameter[],
  boundaryConfigs: Record<string, BoundaryConfig>,
): { params: Parameter[]; error: ErrorInfo } {
  if (Object.keys(boundaryConfigs).length === 0) {
    return { params, error: okError() };
  }
  const configError = validateBoundaryConfigs(boundaryConfigs, params);
  if (configError.code !== ErrorCode.Ok) {
    return { params, error: configError };
  }
  const expanded: Parameter[] = [];
  for (const param of params) {
    if (!Object.hasOwn(boundaryConfigs, param.name)) {
      expanded.push(param);
      continue;
    }
    const metadataError = validateBoundaryMetadata(param);
    if (metadataError.code !== ErrorCode.Ok) {
      return { params, error: metadataError };
    }
    expanded.push(expandBoundaryValues(param, boundaryConfigs[param.name]));
  }
  return { params: expanded, error: okError() };
}

/**
 * Validate generation options before expansion or resource allocation.
 *
 * This is the rule set alone, judging the value space it is handed — not the
 * acceptance gate. The gate is acceptOptions, which runs expandBoundaries
 * before this. Taking this on its own for the gate's answer gets a boundary
 * model wrong in a specific way: a weight or a seed naming a value that
 * expansion is about to supply is refused here, and accepted by every entry
 * point.
 */
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
  const budgetError = validateStringBudget(options, params);
  if (budgetError.code !== ErrorCode.Ok) {
    return budgetError;
  }
  const boundaryError = validateBoundaryConfigs(options.boundaryConfigs, params);
  if (boundaryError.code !== ErrorCode.Ok) {
    return boundaryError;
  }
  const validationParams = params.map((param) => {
    if (param.values.length > 0 || getBoundaryConfig(options, param.name) === undefined) {
      return param;
    }
    return new Parameter(param.name, ['__coverwise_boundary_placeholder__']);
  });
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
  if (options.constraintExpressions.length > MAX_CONSTRAINTS) {
    return invalid(
      `Constraint count ${options.constraintExpressions.length} exceeds maximum of ${MAX_CONSTRAINTS}`,
    );
  }
  if (options.seeds.length > MAX_TESTS) {
    return invalid(`Seed test count ${options.seeds.length} exceeds maximum of ${MAX_TESTS}`);
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

  // Walked in the core's own key order rather than the order the caller wrote
  // the object in, so a map with more than one thing wrong with it is refused
  // over the same key on every surface. The core holds these as std::map, and a
  // JavaScript object holds them in insertion order.
  for (const paramName of Object.keys(options.weights.entries).sort(compareUtf8)) {
    const valueWeights = options.weights.entries[paramName];
    const param = byName.get(paramName);
    if (!param) {
      return invalid(`Unknown parameter in weights: ${paramName}`);
    }
    // Which key has claimed each value so far, so a second key naming the same
    // value is caught here rather than resolved by whichever key the surface's
    // map happened to hand over first.
    const claimedBy = new Map<number, string>();
    for (const valueName of Object.keys(valueWeights).sort(compareUtf8)) {
      const weight = valueWeights[valueName];
      const valueIndex = resolveValueName(param, valueName);
      if (valueIndex === UNASSIGNED) {
        return invalid(`Unknown value in weights: ${paramName}=${valueName}`);
      }
      if (!Number.isFinite(weight) || weight <= 0) {
        return invalid(`Weight must be finite and positive: ${paramName}=${valueName}`);
      }
      // Two keys naming one value carry two weights for it, and only one can
      // apply. A key spelled the way the model declares the value settles that
      // outright, which is how a weight keyed by an alias keeps working beside
      // one keyed by the value itself. With no declared spelling among them the
      // winner would come down to the order the caller's map is walked in, and
      // that order is not the same on every surface — so the model is refused
      // instead of weighted differently depending on where it was run.
      const claimed = claimedBy.get(valueIndex);
      const declared = param.values[valueIndex];
      if (claimed !== undefined && claimed !== declared && valueName !== declared) {
        return invalid(
          `Ambiguous value in weights: ${paramName}=${claimed} and ${paramName}=${valueName} name the same value`,
        );
      }
      if (claimed === undefined || valueName === declared) {
        claimedBy.set(valueIndex, valueName);
      }
    }
  }
  return okError();
}

/**
 * Rebuild the Parameter objects an options object describes.
 *
 * The aliases and equivalence classes carried on the options are restored here,
 * before expansion: expansion regenerates a value set but carries per-value
 * metadata across by value identity, so a retained value keeps its aliases and
 * its class.
 */
export function optionsParameters(options: GenerateOptions): Parameter[] {
  return options.parameters.map((p) => {
    const param = p.invalid
      ? new Parameter(p.name, p.values, p.invalid)
      : new Parameter(p.name, p.values);
    if (p.aliases?.some((a) => a.length > 0)) {
      param.setAliases(p.aliases);
    }
    if (p.equivalenceClasses?.some((c) => c.length > 0)) {
      param.setEquivalenceClasses(p.equivalenceClasses);
    }
    return param;
  });
}

/** Describe a Parameter in the shape an options object carries. */
function toParameterSpec(param: Parameter): GenerateOptions['parameters'][number] {
  const spec: GenerateOptions['parameters'][number] = { name: param.name, values: param.values };
  if (param.invalid.length > 0) {
    spec.invalid = param.invalid;
  }
  if (param.hasAliases) {
    spec.aliases = param.allAliases;
  }
  if (param.hasEquivalenceClasses) {
    spec.equivalenceClasses = param.equivalenceClasses;
  }
  return spec;
}

/**
 * The outcome of submitting options to the acceptance gate.
 *
 * Exactly one side is meaningful: on an ok error the options and the parameters
 * describe the value space the engine will run on, and on a rejection they are
 * the caller's own input, returned unchanged so only `error` has to be read.
 * Mirrors model::AcceptedOptions in the C++ core, which carries the parameters
 * inside the options because a C++ GenerateOptions holds Parameter objects
 * rather than the plain descriptions this port carries.
 */
export interface AcceptedOptions {
  /** Boundary parameters already expanded, and no boundary configs left. */
  options: GenerateOptions;
  /** The expanded parameter set, in the form the engine takes. */
  params: Parameter[];
  error: ErrorInfo;
}

/**
 * Run the acceptance gate: expand boundaries, then validate everything.
 *
 * This is the one description of what the engine accepts, so every surface asks
 * it rather than composing the two halves for itself — and asking it a second
 * time is how a caller checks that a surface really did. Mirrors
 * model::AcceptOptions in the C++ core.
 *
 * Expansion runs first so every later rule is applied to the value space the
 * engine will use. Judging the declared values instead would, for instance,
 * reject a weight naming a value expansion is about to supply.
 *
 * This and the two halves it composes are one unit: expandBoundaries and
 * validateGenerateOptions are not separately meaningful as an acceptance
 * answer, and wherever they live this belongs beside them.
 */
export function acceptOptions(options: GenerateOptions): AcceptedOptions {
  const declared = optionsParameters(options);
  const expansion = expandBoundaries(declared, options.boundaryConfigs);
  if (expansion.error.code !== ErrorCode.Ok) {
    return { options, params: declared, error: expansion.error };
  }
  const accepted: GenerateOptions = {
    ...options,
    parameters: expansion.params.map(toParameterSpec),
    boundaryConfigs: {},
  };
  const validationError = validateGenerateOptions(accepted);
  if (validationError.code !== ErrorCode.Ok) {
    return { options, params: declared, error: validationError };
  }
  return { options: accepted, params: expansion.params, error: okError() };
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
  /**
   * Coarse sizing heuristic from the largest value count, the strength and the
   * parameter count, capped at `totalTuples`. Not a bound in either direction:
   * a generated suite may be smaller or larger.
   */
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
