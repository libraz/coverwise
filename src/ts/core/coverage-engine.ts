/// @file coverage-engine.ts
/// @brief Coverage tracking engine for t-wise tuple coverage.

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from '../model/error.js';
import { hasInvalidValues, type Parameter, UNASSIGNED } from '../model/parameter.js';
import type { TestCase, UncoveredTuple } from '../model/test-case.js';
import { DynamicBitset } from '../util/bitset.js';
import { decodeMixedRadix, generateCombinations } from '../util/combinatorics.js';

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
    engine.initCombinations();
    engine.totalTuples_ = engine.computeTotalTuples();
    engine.buildLookupTables();

    if (engine.totalTuples_ > CoverageEngine.MAX_TUPLES) {
      return {
        engine: new CoverageEngine(),
        error: makeTupleExplosionError(engine.totalTuples_, CoverageEngine.MAX_TUPLES),
      };
    }

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
    engine.initCombinationsFromSubset();
    engine.totalTuples_ = engine.computeTotalTuples();
    engine.buildLookupTables();

    if (engine.totalTuples_ > CoverageEngine.MAX_TUPLES) {
      return {
        engine: new CoverageEngine(),
        error: makeTupleExplosionError(engine.totalTuples_, CoverageEngine.MAX_TUPLES),
      };
    }

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
  excludeInvalidTuples(constraints: readonly ConstraintNode[]): void {
    if (constraints.length === 0) {
      return;
    }

    const numParams = this.params_.length;

    for (let ci = 0; ci < this.paramCombinations_.length; ++ci) {
      const combo = this.paramCombinations_[ci];

      // Compute product for this combination.
      let product = 1;
      for (const pi of combo) {
        product *= this.params_[pi].size;
      }

      // Enumerate all value tuples.
      for (let vi = 0; vi < product; ++vi) {
        const globalIndex = this.combinationOffsets_[ci] + vi;
        if (this.covered_.test(globalIndex)) {
          continue; // Already marked.
        }

        // Decode mixed-radix index into value indices.
        const radixes: number[] = new Array(combo.length);
        for (let j = 0; j < combo.length; ++j) {
          radixes[j] = this.params_[combo[j]].size;
        }
        const valueIndices = decodeMixedRadix(vi, radixes);

        // Build partial assignment with only this tuple's parameters set.
        const assignment = new Array<number>(numParams);
        for (let i = 0; i < numParams; i++) {
          assignment[i] = UNASSIGNED;
        }
        for (let j = 0; j < combo.length; ++j) {
          assignment[combo[j]] = valueIndices[j];
        }

        // Evaluate all constraints against this partial assignment.
        let invalid = false;
        for (const constraint of constraints) {
          const result = constraint.evaluate(assignment);
          if (result === ConstraintResult.False) {
            invalid = true;
            break;
          }
        }

        if (invalid) {
          this.covered_.set(globalIndex);
          ++this.invalidTuples_;
        }
      }
    }
  }

  /// Exclude tuples that contain values marked as invalid in parameters.
  ///
  /// Any tuple containing at least one value where Parameter.isInvalid()
  /// returns true is marked as excluded. Used for positive-only generation.
  excludeInvalidValues(): void {
    if (!hasInvalidValues(this.params_)) {
      return;
    }

    for (let ci = 0; ci < this.paramCombinations_.length; ++ci) {
      const combo = this.paramCombinations_[ci];

      // Compute product for this combination.
      let product = 1;
      for (const pi of combo) {
        product *= this.params_[pi].size;
      }

      // Enumerate all value tuples.
      for (let vi = 0; vi < product; ++vi) {
        const globalIndex = this.combinationOffsets_[ci] + vi;
        if (this.covered_.test(globalIndex)) {
          continue;
        }

        // Decode mixed-radix index into value indices.
        const radixes: number[] = new Array(combo.length);
        for (let j = 0; j < combo.length; ++j) {
          radixes[j] = this.params_[combo[j]].size;
        }
        const valueIndices = decodeMixedRadix(vi, radixes);

        // Check if any decoded value is invalid.
        let containsInvalid = false;
        for (let j = 0; j < combo.length; ++j) {
          if (this.params_[combo[j]].isInvalid(valueIndices[j])) {
            containsInvalid = true;
            break;
          }
        }

        if (containsInvalid) {
          this.covered_.set(globalIndex);
          ++this.invalidTuples_;
        }
      }
    }
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
  getUncoveredTuples(params: Parameter[]): UncoveredTuple[] {
    const result: UncoveredTuple[] = [];

    for (let ci = 0; ci < this.paramCombinations_.length; ++ci) {
      const combo = this.paramCombinations_[ci];

      // Compute the number of value tuples for this combination.
      let product = 1;
      for (const pi of combo) {
        product *= params[pi].size;
      }

      // Enumerate all value tuples using mixed-radix decoding.
      for (let vi = 0; vi < product; ++vi) {
        const globalIndex = this.combinationOffsets_[ci] + vi;
        if (this.covered_.test(globalIndex)) {
          continue;
        }

        // Decode the mixed-radix index into value indices.
        const radixes: number[] = new Array(combo.length);
        for (let j = 0; j < combo.length; ++j) {
          radixes[j] = params[combo[j]].size;
        }
        const valueIndices = decodeMixedRadix(vi, radixes);

        const tuple: string[] = [];
        const paramNames: string[] = [];
        for (let j = 0; j < combo.length; ++j) {
          const pi = combo[j];
          paramNames.push(params[pi].name);
          tuple.push(`${params[pi].name}=${params[pi].values[valueIndices[j]]}`);
        }

        result.push({ tuple, params: paramNames, reason: 'never covered' });
      }
    }

    return result;
  }

  // --- Private methods ---

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

    for (const combo of this.paramCombinations_) {
      this.combinationOffsets_.push(total);
      let product = 1;
      for (const pi of combo) {
        product *= this.params_[pi].size;
      }
      total += product;
    }
    return total;
  }
}

/// Build an error for when tuple count exceeds the safety limit.
function makeTupleExplosionError(totalTuples: number, maxTuples: number): ErrorInfo {
  return {
    code: ErrorCode.TupleExplosion,
    message: 't-wise tuple count exceeds safety limit',
    detail: `Total tuples: ${totalTuples}, limit: ${maxTuples}. Reduce strength or parameter count.`,
  };
}
