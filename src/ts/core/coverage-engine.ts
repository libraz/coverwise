/// @file coverage-engine.ts
/// @brief Coverage tracking engine for t-wise tuple coverage.

import type { ConstraintNode } from '../model/constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from '../model/error.js';
import { hasInvalidValues, type Parameter, UNASSIGNED } from '../model/parameter.js';
import type { TestCase, UncoveredTuple } from '../model/test-case.js';
import { DynamicBitset } from '../util/bitset.js';
import { checkedBinomial, generateCombinationsFlat } from '../util/combinatorics.js';
import {
  buildAllowedSolveParameterOrder,
  buildValidSolveParameterOrder,
  completeAssignment,
  completeValidAssignment,
  createSolveBudget,
} from './constraint-solver.js';

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
  /// Number of set bits in covered_ (covered plus excluded tuples), maintained
  /// incrementally so coveredCount is O(1) instead of rescanning the bitset.
  private coveredBits_ = 0;
  private covered_: DynamicBitset = new DynamicBitset(0);

  /// Mapping from local param index to global param index.
  /// Empty means identity mapping (all params, no subset).
  private paramSubset_: number[] = [];

  /// Pre-computed C(n, t) parameter index combinations, stored flat with stride
  /// strength_ (combo ci occupies [ci*strength_, (ci+1)*strength_)). A flat
  /// buffer avoids one small array per combination near the cap and mirrors the
  /// C++ layout. When paramSubset_ is set, these contain GLOBAL param indices.
  private paramCombinations_: number[] = [];
  private numCombinations_ = 0;
  private combinationOffsets_: number[] = [];

  /// paramToCombos_[p] = list of combination indices that include param p.
  private paramToCombos_: number[][] = [];

  /// paramPositionInCombo_[p][k] = position of p within
  /// combination paramToCombos_[p][k].
  private paramPositionInCombo_: number[][] = [];

  /// Mixed-radix multipliers per combination, stored flat with stride strength_:
  /// mults(ci)[j] = product of value counts for positions j+1..t-1.
  private comboMultipliers_: number[] = [];

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
    for (let ci = 0; ci < this.numCombinations_; ++ci) {
      const base = ci * this.strength_;

      let localIndex = 0;
      for (let j = 0; j < this.strength_; ++j) {
        localIndex +=
          testCase.values[this.paramCombinations_[base + j]] * this.comboMultipliers_[base + j];
      }

      const index = this.combinationOffsets_[ci] + localIndex;
      if (!this.covered_.test(index)) {
        this.covered_.set(index);
        ++this.coveredBits_;
      }
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
      const base = ci * this.strength_;

      // Check all other params are assigned and compute mixed-radix index.
      let allAssigned = true;
      let localIndex = valueIndex * this.comboMultipliers_[base + pos];
      for (let j = 0; j < this.strength_; ++j) {
        if (j === pos) {
          continue;
        }
        const v = partial.values[this.paramCombinations_[base + j]];
        if (v === UNASSIGNED) {
          allAssigned = false;
          break;
        }
        localIndex += v * this.comboMultipliers_[base + j];
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

    for (let ci = 0; ci < this.numCombinations_; ++ci) {
      const base = ci * this.strength_;

      let localIndex = 0;
      for (let j = 0; j < this.strength_; ++j) {
        localIndex +=
          candidate.values[this.paramCombinations_[base + j]] * this.comboMultipliers_[base + j];
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
  /// @param budgetExceeded Optional single-element out array; set to true if any
  ///   per-tuple feasibility search exhausted its node budget, in which case
  ///   exclusion stops early so the caller can surface an explicit error.
  excludeInvalidTuples(
    constraints: readonly ConstraintNode[],
    allowedValues: readonly (readonly boolean[])[] = [],
    budgetExceeded?: { value: boolean },
  ): void {
    if (constraints.length === 0) {
      return;
    }

    const numParams = this.params_.length;
    const parameterOrder =
      allowedValues.length === 0
        ? buildValidSolveParameterOrder(this.params_)
        : buildAllowedSolveParameterOrder(this.params_, allowedValues);
    const assignment = new Array<number>(numParams).fill(UNASSIGNED);

    this.forEachTuple((ci, vi, combo, valueIndices) => {
      // Build partial assignment with only this tuple's parameters set.
      for (let j = 0; j < combo.length; ++j) {
        assignment[combo[j]] = valueIndices[j];
      }

      // Partial evaluation alone misses tuples made impossible by interacting
      // implications. Require a complete valid-value witness instead. Each
      // per-tuple search is bounded; an exhausted budget stops exclusion so the
      // caller can error out rather than proceed on a partially classified set.
      const tupleBudget = createSolveBudget();
      const witness =
        allowedValues.length === 0
          ? completeValidAssignment(
              this.params_,
              constraints,
              { values: assignment },
              tupleBudget,
              parameterOrder,
            )
          : completeAssignment(
              this.params_,
              constraints,
              allowedValues,
              { values: assignment },
              tupleBudget,
              parameterOrder,
            );
      assignment.fill(UNASSIGNED);
      if (tupleBudget.exceeded) {
        if (budgetExceeded !== undefined) {
          budgetExceeded.value = true;
        }
        return false; // Stop; universe is not fully classified.
      }
      const globalIndex = this.combinationOffsets_[ci] + vi;
      if (witness === null && !this.covered_.test(globalIndex)) {
        this.covered_.set(globalIndex);
        ++this.invalidTuples_;
        ++this.coveredBits_;
      }
      return true;
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
            ++this.coveredBits_;
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
        ++this.coveredBits_;
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
        ++this.coveredBits_;
      }
    });
  }

  /// Return the total number of valid t-wise tuples.
  get totalTuples(): number {
    return this.totalTuples_ - this.invalidTuples_;
  }

  /// Return the number of covered valid tuples.
  ///
  /// O(1): coveredBits_ (set bits: covered plus excluded) is tracked
  /// incrementally, so this never rescans the bitset.
  get coveredCount(): number {
    return this.coveredBits_ - this.invalidTuples_;
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
      const indices: Array<[number, number]> = [];
      for (let j = 0; j < combo.length; ++j) {
        const pi = combo[j];
        paramNames.push(params[pi].name);
        tuple.push(`${params[pi].name}=${params[pi].values[valueIndices[j]]}`);
        indices.push([pi, valueIndices[j]]);
      }
      uncovered.push({ tuple, params: paramNames, indices, reason: 'never covered' });
      return uncovered.length < limit;
    });

    return uncovered;
  }

  /// Return the first currently-uncovered tuple as a partial assignment, or null.
  ///
  /// The assignment fixes exactly this tuple's (parameter, value) pairs over the
  /// global parameter space, leaving all other positions UNASSIGNED. Used by the
  /// generator's completion phase to construct a test covering this tuple
  /// directly instead of relying on randomized greedy construction.
  firstUncovered(): { index: number; assignment: number[] } | null {
    let found: { index: number; assignment: number[] } | null = null;
    this.forEachTuple((ci, vi, combo, valueIndices) => {
      const assignment = new Array<number>(this.params_.length).fill(UNASSIGNED);
      for (let j = 0; j < combo.length; ++j) {
        assignment[combo[j]] = valueIndices[j];
      }
      found = { index: this.combinationOffsets_[ci] + vi, assignment };
      return false; // Stop after the first uncovered tuple.
    });
    return found;
  }

  /// Exclude a tuple (by global index) from the coverage target.
  ///
  /// Used when a tuple is partial-feasible but cannot be extended to any complete
  /// constraint-satisfying assignment, so it is genuinely unreachable and must
  /// not count as a coverage shortfall.
  excludeTuple(index: number): void {
    if (!this.covered_.test(index)) {
      this.covered_.set(index);
      ++this.invalidTuples_;
      ++this.coveredBits_;
    }
  }

  /// Clear all coverage and exclusion state, keeping the precomputed combination
  /// and lookup tables intact.
  ///
  /// Lets a single engine be reused across passes that share the same parameters
  /// and strength but apply different exclusions (e.g. negative-test generation,
  /// which fixes a different invalid value each pass), avoiding a full table
  /// rebuild per pass.
  resetCoverage(): void {
    this.covered_.reset();
    this.invalidTuples_ = 0;
    this.coveredBits_ = 0;
  }

  // --- Private methods ---

  /// Iterate over all uncovered tuples, calling fn for each.
  ///
  /// Pre-allocates the radixes array once per combination (not per value tuple)
  /// to reduce allocation pressure. Skips already-covered tuples.
  private forEachTuple(
    fn: (ci: number, vi: number, combo: number[], valueIndices: number[]) => boolean | undefined,
  ): void {
    // Reused buffers so tuple iteration does not allocate per tuple.
    const combo: number[] = new Array(this.strength_);
    const radixes: number[] = new Array(this.strength_);
    const valueIndices: number[] = new Array(this.strength_);
    for (let ci = 0; ci < this.numCombinations_; ++ci) {
      const cbase = ci * this.strength_;
      for (let j = 0; j < this.strength_; ++j) {
        combo[j] = this.paramCombinations_[cbase + j];
      }

      for (let j = 0; j < this.strength_; ++j) {
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

        let remainder = vi;
        for (let index = radixes.length - 1; index >= 0; --index) {
          valueIndices[index] = remainder % radixes[index];
          remainder = Math.floor(remainder / radixes[index]);
        }
        if (fn(ci, vi, combo, valueIndices) === false) {
          return;
        }
      }
    }
  }

  private initCombinations(): void {
    const n = this.params_.length;
    this.paramCombinations_ = generateCombinationsFlat(n, this.strength_);
    this.numCombinations_ = this.paramCombinations_.length / this.strength_;
  }

  private initCombinationsFromSubset(): void {
    const n = this.paramSubset_.length;
    this.paramCombinations_ = generateCombinationsFlat(n, this.strength_);
    for (let i = 0; i < this.paramCombinations_.length; ++i) {
      this.paramCombinations_[i] = this.paramSubset_[this.paramCombinations_[i]];
    }
    this.numCombinations_ = this.paramCombinations_.length / this.strength_;
  }

  private buildLookupTables(): void {
    const numParams = this.params_.length;
    const numCombos = this.numCombinations_;

    // Build param-to-combinations index and position-in-combo lookup.
    this.paramToCombos_ = new Array(numParams);
    this.paramPositionInCombo_ = new Array(numParams);
    for (let i = 0; i < numParams; i++) {
      this.paramToCombos_[i] = [];
      this.paramPositionInCombo_[i] = [];
    }

    for (let ci = 0; ci < numCombos; ++ci) {
      const base = ci * this.strength_;
      for (let j = 0; j < this.strength_; ++j) {
        const pi = this.paramCombinations_[base + j];
        this.paramToCombos_[pi].push(ci);
        this.paramPositionInCombo_[pi].push(j);
      }
    }

    // Build mixed-radix multipliers for each combination, stored flat.
    // mults(ci)[j] = product of value counts for positions j+1..t-1.
    this.comboMultipliers_ = new Array(numCombos * this.strength_);
    for (let ci = 0; ci < numCombos; ++ci) {
      const base = ci * this.strength_;
      this.comboMultipliers_[base + this.strength_ - 1] = 1;
      for (let j = this.strength_ - 2; j >= 0; --j) {
        this.comboMultipliers_[base + j] =
          this.comboMultipliers_[base + j + 1] *
          this.params_[this.paramCombinations_[base + j + 1]].size;
      }
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
    for (let ci = 0; ci < this.numCombinations_; ++ci) {
      const base = ci * this.strength_;
      this.combinationOffsets_.push(total);
      let product = 1;
      for (let j = 0; j < this.strength_; ++j) {
        product *= this.params_[this.paramCombinations_[base + j]].size;
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
    // Compute the full product for this combination so the reported figure
    // reflects the real (approximate) magnitude rather than a fixed sentinel just
    // past the limit.
    let product = 1;
    for (const local of combo) {
      const pi = subset === null ? local : subset[local];
      product *= params[pi].size;
    }
    if (product > CoverageEngine.MAX_TUPLES) {
      return { total: 0, error: makeTupleExplosionError(product, CoverageEngine.MAX_TUPLES) };
    }
    total += product;
    if (total > CoverageEngine.MAX_TUPLES) {
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
