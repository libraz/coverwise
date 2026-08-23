/// @file greedy.ts
/// @brief Constructive greedy algorithm for covering array construction.
///
/// Builds test cases parameter-by-parameter, choosing the value that
/// maximizes coverage gain at each step. This is O(n * max_values)
/// per test case instead of O(product of values).

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { UNASSIGNED } from '../model/parameter.js';
import type { TestCase } from '../model/test-case.js';
import type { Rng } from '../util/rng.js';

/// Scoring function: adds the coverage gain of every value of `paramIndex` into
/// `outScores[valueIndex]`, given the current partial test case. Scoring a whole
/// parameter at once keeps the work per candidate value constant; scoring values
/// one at a time repeats the sweep over the parameter's combinations per value.
export type ScoreValuesFn = (partial: TestCase, paramIndex: number, outScores: number[]) => void;

/// Minimal parameter interface needed by the greedy algorithm.
export interface GreedyParam {
  readonly size: number;
}

/// Buffers a greedy construction borrows instead of allocating its own.
///
/// One instance is owned by the generation pass and handed to every
/// greedyConstruct() call, so building a suite allocates a bounded amount rather
/// than one array per parameter per test case. The contents carry no meaning
/// between calls.
export interface GreedyScratch {
  /// Parameter order, reshuffled per construction.
  order: number[];
  /// Coverage gain per value of the current parameter.
  scores: number[];
  /// Value indices tied for the best score.
  bestValues: number[];
}

/// A constructed test case together with the coverage it gains.
export interface GreedyResult {
  testCase: TestCase;
  /// Newly covered tuples, accumulated while the test case was built.
  ///
  /// Each parameter combination is scored exactly once — at the step that
  /// assigns the last of its parameters — and the coverage state does not change
  /// during a construction, so this equals scoring the finished test case
  /// against that same state.
  score: number;
}

/// Create scratch buffers sized for `params` so no construction has to grow them.
export function createGreedyScratch(params: readonly GreedyParam[]): GreedyScratch {
  let maxValues = 0;
  for (const param of params) {
    maxValues = Math.max(maxValues, param.size);
  }
  return {
    order: new Array<number>(params.length).fill(0),
    scores: new Array<number>(maxValues).fill(0),
    bestValues: [],
  };
}

/// Break ties among bestValues using weights, then RNG for remaining ties.
///
/// Uses weighted random selection: each tied value's probability is proportional
/// to its weight. This biases toward higher-weighted values while maintaining
/// enough randomness for the greedy algorithm to explore diverse test cases.
function breakTieWithWeights(
  bestValues: number[],
  weights: number[][],
  pi: number,
  rng: Rng,
): number {
  if (bestValues.length === 1) {
    return bestValues[0];
  }
  if (weights.length > 0) {
    // Normalize first so a finite sum cannot overflow to Infinity.
    let maxWeight = 0.0;
    for (const vi of bestValues) {
      maxWeight = Math.max(maxWeight, weights[pi][vi]);
    }
    let totalWeight = 0.0;
    if (maxWeight > 0.0) {
      for (const vi of bestValues) {
        totalWeight += weights[pi][vi] / maxWeight;
      }
    }
    if (totalWeight > 0.0) {
      // Generate a random value in [0, totalWeight).
      const r = (rng.nextUint32(1000000) / 1000000.0) * totalWeight;
      let cumulative = 0.0;
      for (const vi of bestValues) {
        cumulative += weights[pi][vi] / maxWeight;
        if (r < cumulative) {
          return vi;
        }
      }
      // Fallback to last value (floating point edge case).
      return bestValues[bestValues.length - 1];
    }
  }
  // No weights or zero total: random tie-break.
  const idx = rng.nextUint32(bestValues.length);
  return bestValues[idx];
}

/// Build a test case parameter-by-parameter using greedy value selection.
///
/// For each parameter (in shuffled order), evaluate all possible values and pick
/// the one that would cover the most uncovered tuples. Ties broken by weights then RNG.
///
/// Constraints are evaluated at each step using three-valued logic:
/// - true: continue
/// - false: skip this value (prune)
/// - unknown: continue (not all params assigned yet)
///
/// This is a single-pass construction: once a parameter is assigned it is never
/// revisited (no backtracking). Under adversarial constraints a locally-greedy
/// choice can therefore leave some satisfiable tuples uncovered. This is a
/// deliberate approximation in favour of speed; the caller bounds the number of
/// retries and reports any resulting shortfall (coverage < 1.0 plus a warning)
/// rather than guaranteeing optimal coverage.
///
/// @param params Parameter definitions (only .size is used).
/// @param scoreValues Scoring function that adds the coverage gain of every
///   value of a parameter into a caller-owned buffer.
/// @param constraints Active constraints (empty if none).
/// @param rng Random number generator for tie-breaking and parameter ordering.
/// @param scratch Caller-owned buffers, reused across constructions.
/// @param allowedValues Optional per-parameter mask of allowed values.
///   If non-empty, allowedValues[pi][vi] must be true for value vi of param pi
///   to be considered. If empty, all values are allowed.
/// @param weights Optional per-parameter per-value weights for tie-breaking.
///   If non-empty, weights[pi][vi] is the weight for value vi of param pi.
/// @returns The constructed test case and its coverage gain, or null if no
///   constraint-satisfying value exists for some parameter. A
///   constraint-violating value is never written into the returned test case.
export function greedyConstruct(
  params: readonly GreedyParam[],
  scoreValues: ScoreValuesFn,
  constraints: readonly ConstraintNode[],
  rng: Rng,
  scratch: GreedyScratch,
  allowedValues: boolean[][] = [],
  weights: number[][] = [],
): GreedyResult | null {
  const numParams = params.length;

  const values = new Array<number>(numParams);
  for (let i = 0; i < numParams; i++) {
    values[i] = UNASSIGNED;
  }
  const tc: TestCase = { values };
  let totalGain = 0;

  // Fisher-Yates shuffle for parameter order.
  const order = scratch.order;
  order.length = numParams;
  for (let i = 0; i < numParams; i++) {
    order[i] = i;
  }
  for (let i = numParams; i > 1; --i) {
    const j = rng.nextUint32(i);
    const tmp = order[i - 1];
    order[i - 1] = order[j];
    order[j] = tmp;
  }

  // Single-pass, no-backtracking construction: each parameter is assigned once in
  // shuffled order. This is a deliberate approximation favouring speed — a greedy
  // local choice may leave some satisfiable tuples uncovered, which the caller
  // surfaces as coverage < 1.0 rather than retrying exhaustively.
  for (const pi of order) {
    const numValues = params[pi].size;
    const scores = scratch.scores;
    scores.length = numValues;
    scores.fill(0);
    scoreValues(tc, pi, scores);

    let bestScore = 0;
    const bestValues = scratch.bestValues;
    bestValues.length = 0;

    for (let vi = 0; vi < numValues; ++vi) {
      if (allowedValues.length > 0 && !allowedValues[pi][vi]) {
        continue;
      }

      // Temporarily assign value for constraint evaluation.
      tc.values[pi] = vi;

      // Evaluate constraints using three-valued logic.
      let pruned = false;
      for (const constraint of constraints) {
        const result = constraint.evaluate(tc.values);
        if (result === ConstraintResult.False) {
          pruned = true;
          break;
        }
        // True and Unknown: continue.
      }

      // Reset before deciding.
      tc.values[pi] = UNASSIGNED;

      if (pruned) {
        continue;
      }

      const score = scores[vi];
      if (bestValues.length === 0 || score > bestScore) {
        bestScore = score;
        bestValues.length = 0;
        bestValues.push(vi);
      } else if (score === bestScore) {
        bestValues.push(vi);
      }
    }

    // Every value allowed by the mask that survives the constraints is recorded
    // above, so an empty set means this parameter has no usable value at all.
    // Constraint evaluation is a pure function of the same partial assignment,
    // so no second pass could reach a different verdict.
    if (bestValues.length === 0) {
      return null;
    }

    tc.values[pi] = breakTieWithWeights(bestValues, weights, pi, rng);
    totalGain += bestScore;
  }

  return { testCase: tc, score: totalGain };
}
