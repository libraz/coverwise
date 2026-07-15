/// @file coverage-engine.ts
/// @brief Coverage tracking engine for t-wise tuple coverage.

import type { ConstraintNode } from '../model/constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from '../model/error.js';
import { hasInvalidValues, type Parameter, UNASSIGNED } from '../model/parameter.js';
import type { TestCase, UncoveredTuple } from '../model/test-case.js';
import { DynamicBitset } from '../util/bitset.js';
import { checkedBinomial, decodeMixedRadix, generateCombinations } from '../util/combinatorics.js';
import { completeAssignment, completeValidAssignment } from './constraint-solver.js';

/// Result of CoverageEngine.create() factory method.
export interface CreateResult {
  engine: CoverageEngine;
  error: ErrorInfo;
}

/// Tracks which t-wise tuples are covered by the current test suite.
///
/// Hard limit on total tuple count to prevent t-wise explosion.
export class CoverageEngine {
  /// Maximum number of tuples before refusing to proceed.
  /// ~16M tuples. Beyond this, performance degrades.
  static readonly MAX_TUPLES = 16_000_000;
  static readonly MAX_COMBINATIONS = 1_000_000;
  static readonly MAX_DIAGNOSTIC_TUPLES = 1_000;

  private params_: Parameter[] = [];
  private strength_ = 0;
  private totalTuples_ = 0;
  private invalidTuples_ = 0;
  private covered_: DynamicBitset = new DynamicBitset(0);

  /// Mapping from local param index to global param index.
  /// Empty means identity mapping (all params, no subset).
  private paramSubset_: number[] = [];

  /// Pre-computed C(n, t) parameter index combinations.
  /// When paramSubset_ is set, these contain GLOBAL param indices.
  private paramCombinations_: number[][] = [];
  private combinationOffsets_: number[] = [];

  /// paramToCombos_[p] = list of combination indices that include param p.
  private paramToCombos_: number[][] = [];

  /// paramPositionInCombo_[p][k] = position of p within
  /// paramCombinations_[paramToCombos_[p][k]].
  private paramPositionInCombo_: number[][] = [];

  /// comboMultipliers_[ci][j] = product of value counts for positions j+1..t-1.
  private comboMultipliers_: number[][] = [];

  private constructor() {}

  /// Initialize coverage tracking for the given parameters and strength.
  /// @param params The parameter definitions.
  /// @param strength The interaction strength (t). 2 = pairwise.
  /// @returns Object with engine and error. Check error.code before using engine.
  static create(params: Parameter[], strength: number): CreateResult {
    const engine = new CoverageEngine();
    engine.params_ = params.slice();
    engine.strength_ = strength;
    const preflight = preflightModel(params, null, strength);
    if (preflight.error.code !== ErrorCode.Ok) {
      return { engine: new CoverageEngine(), error: preflight.error };
    }
    engine.initCombinations();
    engine.totalTuples_ = engine.computeTotalTuples();

    if (engine.totalTuples_ > CoverageEngine.MAX_TUPLES) {
      return {
        engine: new CoverageEngine(),
        error: makeTupleExplosionError(engine.totalTuples_, CoverageEngine.MAX_TUPLES),
      };
    }

    engine.buildLookupTables();
    engine.covered_ = new DynamicBitset(engine.totalTuples_);
    return { engine, error: okError() };
  }

  /// Initialize coverage tracking for a subset of parameters.
  ///
  /// Only the parameters at the given indices are considered for tuple
  /// generation. Test cases still use global parameter indices.
  /// @param allParams All parameter definitions.
  /// @param paramSubset Indices of parameters to cover (must be sorted).
  /// @param strength The interaction strength (t).
  /// @returns Object with engine and error. Check error.code before using engine.
  static createFromSubset(
    allParams: Parameter[],
    paramSubset: number[],
    strength: number,
  ): CreateResult {
    const engine = new CoverageEngine();
    engine.params_ = allParams.slice();
    engine.strength_ = strength;
    engine.paramSubset_ = paramSubset.slice();
    const preflight = preflightModel(allParams, paramSubset, strength);
    if (preflight.error.code !== ErrorCode.Ok) {
      return { engine: new CoverageEngine(), error: preflight.error };
    }
    engine.initCombinationsFromSubset();
    engine.totalTuples_ = engine.computeTotalTuples();

    if (engine.totalTuples_ > CoverageEngine.MAX_TUPLES) {
      return {
        engine: new CoverageEngine(),
        error: makeTupleExplosionError(engine.totalTuples_, CoverageEngine.MAX_TUPLES),
      };
    }

    engine.buildLookupTables();
    engine.covered_ = new DynamicBitset(engine.totalTuples_);
    return { engine, error: okError() };
  }

  /// Mark all tuples covered by the given test case.
  addTestCase(testCase: TestCase): void {
    for (let ci = 0; ci < this.paramCombinations_.length; ++ci) {
      const combo = this.paramCombinations_[ci];
      const mults = this.comboMultipliers_[ci];

      let localIndex = 0;
      for (let j = 0; j < this.strength_; ++j) {
        localIndex += testCase.values[combo[j]] * mults[j];
      }

      this.covered_.set(this.combinationOffsets_[ci] + localIndex);
    }
  }

  /// Score a candidate value for a single parameter position.
  ///
  /// Used by constructive greedy: given a partial assignment, how many new
  /// tuples would be covered by setting paramIndex to valueIndex?
  scoreValue(partial: TestCase, paramIndex: number, valueIndex: number): number {
    let score = 0;
    const relevantCombos = this.paramToCombos_[paramIndex];
    const positions = this.paramPositionInCombo_[paramIndex];
    const numRelevant = relevantCombos.length;

    for (let k = 0; k < numRelevant; ++k) {
      const ci = relevantCombos[k];
      const pos = positions[k];
      const combo = this.paramCombinations_[ci];
      const mults = this.comboMultipliers_[ci];

      // Check all other params are assigned and compute mixed-radix index.
      let allAssigned = true;
      let localIndex = valueIndex * mults[pos];
      for (let j = 0; j < this.strength_; ++j) {
        if (j === pos) {
          continue;
        }
        const v = partial.values[combo[j]];
        if (v === UNASSIGNED) {
          allAssigned = false;
          break;
        }
        localIndex += v * mults[j];
      }
      if (!allAssigned) {
        continue;
      }

      if (!this.covered_.test(this.combinationOffsets_[ci] + localIndex)) {
        ++score;
      }
    }

    return score;
  }

  /// Score a complete candidate test case.
  scoreCandidate(candidate: TestCase): number {
    let score = 0;

    for (let ci = 0; ci < this.paramCombinations_.length; ++ci) {
      const combo = this.paramCombinations_[ci];
      const mults = this.comboMultipliers_[ci];

      let localIndex = 0;
      for (let j = 0; j < this.strength_; ++j) {
        localIndex += candidate.values[combo[j]] * mults[j];
      }

      if (!this.covered_.test(this.combinationOffsets_[ci] + localIndex)) {
        ++score;
      }
    }

    return score;
  }

  /// Exclude tuples that are invalid due to constraints.
  ///
  /// For each t-tuple, builds a partial assignment and evaluates all
  /// constraints. If any constraint returns False, the tuple is marked
  /// as covered (excluded) and does not count toward coverage goals.
  excludeInvalidTuples(
    constraints: readonly ConstraintNode[],
    allowedValues: readonly (readonly boolean[])[] = [],
  ): void {
    if (constraints.length === 0) {
      return;
    }

    const numParams = this.params_.length;

    this.forEachTuple((ci, vi, combo, valueIndices) => {
      // Build partial assignment with only this tuple's parameters set.
      const assignment = new Array<number>(numParams);
      for (let i = 0; i < numParams; i++) {
        assignment[i] = UNASSIGNED;
      }
      for (let j = 0; j < combo.length; ++j) {
        assignment[combo[j]] = valueIndices[j];
      }

      // Partial evaluation alone misses tuples made impossible by interacting
      // implications. Require a complete valid-value witness instead.
      const witness =
        allowedValues.length === 0
          ? completeValidAssignment(this.params_, constraints, { values: assignment })
          : completeAssignment(this.params_, constraints, allowedValues, { values: assignment });
      const globalIndex = this.combinationOffsets_[ci] + vi;
      if (witness === null && !this.covered_.test(globalIndex)) {
        this.covered_.set(globalIndex);
        ++this.invalidTuples_;
      }
    });
  }

  /// Exclude tuples that contain values marked as invalid in parameters.
  ///
  /// Any tuple containing at least one value where Parameter.isInvalid()
  /// returns true is marked as excluded. Used for positive-only generation.
  excludeInvalidValues(): void {
    if (!hasInvalidValues(this.params_)) {
      return;
    }

    this.forEachTuple((ci, vi, combo, valueIndices) => {
      for (let j = 0; j < combo.length; ++j) {
        if (this.params_[combo[j]].isInvalid(valueIndices[j])) {
          const globalIndex = this.combinationOffsets_[ci] + vi;
          if (!this.covered_.test(globalIndex)) {
            this.covered_.set(globalIndex);
            ++this.invalidTuples_;
          }
          return;
        }
      }
    });
  }

  /// Exclude tuples containing any value disallowed by the mask.
  excludeTuplesOutsideMask(allowedValues: readonly (readonly boolean[])[]): void {
    if (allowedValues.length !== this.params_.length) {
      return;
    }
    this.forEachTuple((ci, vi, combo, valueIndices) => {
      const excluded = combo.some(
        (pi, j) =>
          allowedValues[pi].length !== this.params_[pi].size || !allowedValues[pi][valueIndices[j]],
      );
      const globalIndex = this.combinationOffsets_[ci] + vi;
      if (excluded && !this.covered_.test(globalIndex)) {
        this.covered_.set(globalIndex);
        ++this.invalidTuples_;
      }
    });
  }

  /// Exclude tuples that do not contain a fixed parameter/value pair.
  excludeTuplesNotContaining(paramIndex: number, valueIndex: number): void {
    this.forEachTuple((ci, vi, combo, valueIndices) => {
      const contains = combo.some((pi, j) => pi === paramIndex && valueIndices[j] === valueIndex);
      const globalIndex = this.combinationOffsets_[ci] + vi;
      if (!contains && !this.covered_.test(globalIndex)) {
        this.covered_.set(globalIndex);
        ++this.invalidTuples_;
      }
    });
  }

  /// Return the total number of valid t-wise tuples.
  get totalTuples(): number {
    return this.totalTuples_ - this.invalidTuples_;
  }

  /// Return the number of covered valid tuples.
  get coveredCount(): number {
    return this.covered_.count() - this.invalidTuples_;
  }

  /// Return coverage ratio [0.0, 1.0].
  get coverageRatio(): number {
    if (this.totalTuples === 0) {
      return 1.0;
    }
    return this.coveredCount / this.totalTuples;
  }

  /// Check if all valid tuples are covered.
  get isComplete(): boolean {
    return this.coveredCount === this.totalTuples;
  }

  /// Collect all uncovered tuples as human-readable objects.
  /// @param params Parameter definitions (for resolving names and values).
  getUncoveredTuples(
    params: Parameter[],
    limit = CoverageEngine.MAX_DIAGNOSTIC_TUPLES,
  ): UncoveredTuple[] {
    const uncovered: UncoveredTuple[] = [];

    this.forEachTuple((_ci, _vi, combo, valueIndices) => {
      if (uncovered.length >= limit) {
        return false;
      }
      const tuple: string[] = [];
      const paramNames: string[] = [];
      for (let j = 0; j < combo.length; ++j) {
        const pi = combo[j];
        paramNames.push(params[pi].name);
        tuple.push(`${params[pi].name}=${params[pi].values[valueIndices[j]]}`);
      }
      uncovered.push({ tuple, params: paramNames, reason: 'never covered' });
      return uncovered.length < limit;
    });

    return uncovered;
  }

  // --- Private methods ---

  /// Iterate over all uncovered tuples, calling fn for each.
  ///
  /// Pre-allocates the radixes array once per combination (not per value tuple)
  /// to reduce allocation pressure. Skips already-covered tuples.
  private forEachTuple(
    fn: (ci: number, vi: number, combo: number[], valueIndices: number[]) => boolean | undefined,
  ): void {
    for (let ci = 0; ci < this.paramCombinations_.length; ++ci) {
      const combo = this.paramCombinations_[ci];

      // Pre-allocate radixes array once per combination.
      const radixes: number[] = new Array(combo.length);
      for (let j = 0; j < combo.length; ++j) {
        radixes[j] = this.params_[combo[j]].size;
      }

      // Compute product for this combination.
      let product = 1;
      for (let j = 0; j < radixes.length; ++j) {
        product *= radixes[j];
      }

      // Enumerate all value tuples.
      for (let vi = 0; vi < product; ++vi) {
        const globalIndex = this.combinationOffsets_[ci] + vi;
        if (this.covered_.test(globalIndex)) {
          continue;
        }

        const valueIndices = decodeMixedRadix(vi, radixes);
        if (fn(ci, vi, combo, valueIndices) === false) {
          return;
        }
      }
    }
  }

  private initCombinations(): void {
    const n = this.params_.length;
    this.paramCombinations_ = generateCombinations(n, this.strength_);
  }

  private initCombinationsFromSubset(): void {
    const n = this.paramSubset_.length;
    const localCombos = generateCombinations(n, this.strength_);

    // Map local indices to global param indices.
    this.paramCombinations_ = [];
    for (const local of localCombos) {
      const globalCombo = new Array<number>(this.strength_);
      for (let i = 0; i < this.strength_; ++i) {
        globalCombo[i] = this.paramSubset_[local[i]];
      }
      this.paramCombinations_.push(globalCombo);
    }
  }

  private buildLookupTables(): void {
    const numParams = this.params_.length;
    const numCombos = this.paramCombinations_.length;

    // Build param-to-combinations index and position-in-combo lookup.
    this.paramToCombos_ = new Array(numParams);
    this.paramPositionInCombo_ = new Array(numParams);
    for (let i = 0; i < numParams; i++) {
      this.paramToCombos_[i] = [];
      this.paramPositionInCombo_[i] = [];
    }

    for (let ci = 0; ci < numCombos; ++ci) {
      const combo = this.paramCombinations_[ci];
      for (let j = 0; j < this.strength_; ++j) {
        const pi = combo[j];
        this.paramToCombos_[pi].push(ci);
        this.paramPositionInCombo_[pi].push(j);
      }
    }

    // Build mixed-radix multipliers for each combination.
    // comboMultipliers_[ci][j] = product of value counts for positions j+1..t-1.
    this.comboMultipliers_ = new Array(numCombos);
    for (let ci = 0; ci < numCombos; ++ci) {
      const combo = this.paramCombinations_[ci];
      const mults = new Array<number>(this.strength_);
      mults[this.strength_ - 1] = 1;
      for (let j = this.strength_ - 2; j >= 0; --j) {
        mults[j] = mults[j + 1] * this.params_[combo[j + 1]].size;
      }
      this.comboMultipliers_[ci] = mults;
    }
  }

  private computeTotalTuples(): number {
    let total = 0;
    this.combinationOffsets_ = [];

    // Mirror the C++ CoverageEngine::ComputeTotalTuples clamping behavior: once
    // the running product or total exceeds MAX_TUPLES, stop accumulating and
    // return a value above the limit. create() then surfaces a single structured
    // TupleExplosion error (instead of throwing a raw Error mid-computation), so
    // the explosion path is identical across C++ and TypeScript surfaces.
    for (const combo of this.paramCombinations_) {
      this.combinationOffsets_.push(total);
      let product = 1;
      for (const pi of combo) {
        product *= this.params_[pi].size;
        if (product > CoverageEngine.MAX_TUPLES) {
          return Math.min(total + product, Number.MAX_SAFE_INTEGER);
        }
      }
      total += product;
      if (total > CoverageEngine.MAX_TUPLES) {
        return Math.min(total, Number.MAX_SAFE_INTEGER);
      }
    }
    return total;
  }
}

function preflightModel(
  params: Parameter[],
  subset: number[] | null,
  strength: number,
): { total: number; error: ErrorInfo } {
  const n = subset === null ? params.length : subset.length;
  if (strength === 0 || strength > n) {
    return { total: 0, error: okError() };
  }
  if (checkedBinomial(n, strength, CoverageEngine.MAX_COMBINATIONS) === null) {
    return {
      total: 0,
      error: {
        code: ErrorCode.TupleExplosion,
        message: 'parameter combination metadata exceeds safety limit',
        detail: `Combinations exceed limit: ${CoverageEngine.MAX_COMBINATIONS}. Reduce strength or parameter count.`,
      },
    };
  }

  const combo = Array.from({ length: strength }, (_, index) => index);
  let total = 0;
  for (;;) {
    let product = 1;
    for (const local of combo) {
      const pi = subset === null ? local : subset[local];
      product *= params[pi].size;
      if (!Number.isSafeInteger(product) || product > CoverageEngine.MAX_TUPLES) {
        return {
          total: 0,
          error: makeTupleExplosionError(CoverageEngine.MAX_TUPLES + 1, CoverageEngine.MAX_TUPLES),
        };
      }
    }
    total += product;
    if (!Number.isSafeInteger(total) || total > CoverageEngine.MAX_TUPLES) {
      return { total: 0, error: makeTupleExplosionError(total, CoverageEngine.MAX_TUPLES) };
    }

    let pos = strength - 1;
    while (pos >= 0 && combo[pos] === n - strength + pos) {
      --pos;
    }
    if (pos < 0) {
      break;
    }
    ++combo[pos];
    for (let index = pos + 1; index < strength; ++index) {
      combo[index] = combo[index - 1] + 1;
    }
  }
  return { total, error: okError() };
}

/// Build an error for when tuple count exceeds the safety limit.
function makeTupleExplosionError(totalTuples: number, maxTuples: number): ErrorInfo {
  return {
    code: ErrorCode.TupleExplosion,
    message: 't-wise tuple count exceeds safety limit',
    detail: `Total tuples: ${totalTuples}, limit: ${maxTuples}. Reduce strength or parameter count.`,
  };
}
