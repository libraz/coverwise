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

/// Scoring function: given partial test case, param index, value index -> coverage score.
export type ScoreFn = (partial: TestCase, paramIndex: number, valueIndex: number) => number;

/// Minimal parameter interface needed by the greedy algorithm.
export interface GreedyParam {
  readonly size: number;
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
    // Weighted random selection: probability proportional to weight.
    let totalWeight = 0.0;
    for (const vi of bestValues) {
      totalWeight += weights[pi][vi];
    }
    if (totalWeight > 0.0) {
      // Generate a random value in [0, totalWeight).
      const r = (rng.nextUint32(1000000) / 1000000.0) * totalWeight;
      let cumulative = 0.0;
      for (const vi of bestValues) {
        cumulative += weights[pi][vi];
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
/// @param params Parameter definitions (only .size is used).
/// @param scoreFn Scoring function that returns the coverage gain for assigning
///   value vi to parameter pi given the current partial test case.
/// @param constraints Active constraints (empty if none).
/// @param rng Random number generator for tie-breaking and parameter ordering.
/// @param allowedValues Optional per-parameter mask of allowed values.
///   If non-empty, allowedValues[pi][vi] must be true for value vi of param pi
///   to be considered. If empty, all values are allowed.
/// @param weights Optional per-parameter per-value weights for tie-breaking.
///   If non-empty, weights[pi][vi] is the weight for value vi of param pi.
export function greedyConstruct(
  params: readonly GreedyParam[],
  scoreFn: ScoreFn,
  constraints: readonly ConstraintNode[],
  rng: Rng,
  allowedValues: boolean[][] = [],
  weights: number[][] = [],
): TestCase {
  const numParams = params.length;

  const values = new Array<number>(numParams);
  for (let i = 0; i < numParams; i++) {
    values[i] = UNASSIGNED;
  }
  const tc: TestCase = { values };

  // Fisher-Yates shuffle for parameter order.
  const order = new Array<number>(numParams);
  for (let i = 0; i < numParams; i++) {
    order[i] = i;
  }
  for (let i = numParams; i > 1; --i) {
    const j = rng.nextUint32(i);
    const tmp = order[i - 1];
    order[i - 1] = order[j];
    order[j] = tmp;
  }

  for (const pi of order) {
    let bestScore = 0;
    const bestValues: number[] = [];

    for (let vi = 0; vi < params[pi].size; ++vi) {
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

      const score = scoreFn(tc, pi, vi);
      if (bestValues.length === 0 || score > bestScore) {
        bestScore = score;
        bestValues.length = 0;
        bestValues.push(vi);
      } else if (score === bestScore) {
        bestValues.push(vi);
      }
    }

    if (bestValues.length === 0) {
      // Fallback: pick the first allowed value that satisfies constraints.
      let assigned = false;
      const candidates =
        allowedValues.length > 0
          ? Array.from({ length: params[pi].size }, (_, i) => i).filter(
              (vi) => allowedValues[pi][vi],
            )
          : Array.from({ length: params[pi].size }, (_, i) => i);

      for (const vi of candidates) {
        tc.values[pi] = vi;
        let pruned = false;
        for (const constraint of constraints) {
          const result = constraint.evaluate(tc.values);
          if (result === ConstraintResult.False) {
            pruned = true;
            break;
          }
        }
        if (!pruned) {
          assigned = true;
          break;
        }
        tc.values[pi] = UNASSIGNED;
      }

      if (!assigned) {
        // No value satisfies constraints; pick the first allowed value anyway.
        tc.values[pi] = candidates.length > 0 ? candidates[0] : 0;
      }
    } else {
      tc.values[pi] = breakTieWithWeights(bestValues, weights, pi, rng);
    }
  }

  return tc;
}
