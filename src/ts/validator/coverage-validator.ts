/// Independent coverage validation (does NOT depend on generator/core).

import type { Parameter } from '../model/parameter.js';
import type { GenerateResult, TestCase, UncoveredTuple } from '../model/test-case.js';

/** Coverage validation report with human-readable uncovered tuples. */
export interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;
  uncovered: UncoveredTuple[];
}

/** Equivalence class coverage report. */
export interface ClassCoverageReport {
  totalClassTuples: number;
  coveredClassTuples: number;
  coverageRatio: number;
}

/**
 * Generate all C(n, k) combinations of indices [0, n).
 * @returns Array of sorted index arrays. Empty if k == 0 or k > n.
 */
function generateCombinations(n: number, k: number): number[][] {
  if (k === 0 || k > n) {
    return [];
  }

  const result: number[][] = [];
  const combo = new Array<number>(k);

  function recurse(start: number, depth: number): void {
    if (depth === k) {
      result.push(combo.slice());
      return;
    }
    for (let i = start; i < n; ++i) {
      combo[depth] = i;
      recurse(i + 1, depth + 1);
    }
  }

  recurse(0, 0);
  return result;
}

/**
 * Check if a test case covers a specific value tuple for given parameter indices.
 */
function testCovers(test: TestCase, paramIndices: number[], valueIndices: number[]): boolean {
  for (let i = 0; i < paramIndices.length; ++i) {
    const pi = paramIndices[i];
    if (pi >= test.values.length || test.values[pi] !== valueIndices[i]) {
      return false;
    }
  }
  return true;
}

/**
 * Independently validate t-wise coverage of a test suite.
 *
 * This validator enumerates all t-tuples from scratch (not using any
 * generator internals) and checks each against the test suite.
 */
export function validateCoverage(
  params: Parameter[],
  tests: TestCase[],
  strength: number,
): CoverageReport {
  const report: CoverageReport = {
    totalTuples: 0,
    coveredTuples: 0,
    coverageRatio: 0,
    uncovered: [],
  };

  const n = params.length;

  // Edge case: strength is 0 or exceeds parameter count.
  // Vacuous coverage: nothing to cover means everything is covered.
  if (strength === 0 || strength > n) {
    report.coverageRatio = 1.0;
    return report;
  }

  // Step 1: Generate all C(n, strength) combinations of parameter indices.
  const combinations = generateCombinations(n, strength);

  for (const combo of combinations) {
    // Step 2: Enumerate all value tuples (cartesian product) for this combination.
    let numTuples = 1;
    for (const pi of combo) {
      numTuples *= params[pi].size;
    }

    // Iterate over all value tuples using a flat index.
    for (let flat = 0; flat < numTuples; ++flat) {
      // Decode flat index into value indices (mixed-radix decomposition).
      const valueIndices = new Array<number>(strength);
      let remainder = flat;
      for (let i = strength - 1; i >= 0; --i) {
        const radix = params[combo[i]].size;
        valueIndices[i] = remainder % radix;
        remainder = Math.trunc(remainder / radix);
      }

      ++report.totalTuples;

      // Step 3: Check if any test case covers this value tuple.
      let covered = false;
      for (const test of tests) {
        if (testCovers(test, combo, valueIndices)) {
          covered = true;
          break;
        }
      }

      if (covered) {
        ++report.coveredTuples;
      } else {
        // Build the UncoveredTuple with human-readable strings.
        const tuple: string[] = [];
        const paramNames: string[] = [];
        for (let i = 0; i < strength; ++i) {
          const pi = combo[i];
          const vi = valueIndices[i];
          paramNames.push(params[pi].name);
          tuple.push(`${params[pi].name}=${params[pi].values[vi]}`);
        }
        report.uncovered.push({
          tuple,
          params: paramNames,
          reason: 'never covered',
        });
      }
    }
  }

  // Compute coverage ratio. When there are no tuples, coverage is vacuously 1.0.
  if (report.totalTuples === 0) {
    report.coverageRatio = 1.0;
  } else {
    report.coverageRatio = report.coveredTuples / report.totalTuples;
  }

  return report;
}

/**
 * Compute equivalence class coverage for a test suite.
 *
 * Maps each value to its equivalence class and enumerates all t-wise class
 * tuples, counting how many are covered by the test suite.
 * Only considers parameters that have equivalence classes defined.
 * @returns Class coverage report. If no parameters have classes, returns all zeros.
 */
export function computeClassCoverage(
  params: Parameter[],
  tests: TestCase[],
  strength: number,
): ClassCoverageReport {
  const report: ClassCoverageReport = {
    totalClassTuples: 0,
    coveredClassTuples: 0,
    coverageRatio: 0,
  };

  const n = params.length;

  if (strength === 0 || strength > n) {
    return report;
  }

  // Identify parameters that have equivalence classes.
  const classParams: number[] = [];
  for (let i = 0; i < n; ++i) {
    if (params[i].hasEquivalenceClasses) {
      classParams.push(i);
    }
  }

  if (classParams.length === 0) {
    return report;
  }

  // For class coverage we consider combinations of parameters that have classes.
  // If fewer parameters have classes than the strength, use the available count.
  const classN = classParams.length;
  const effectiveStrength = Math.min(strength, classN);

  // Generate all C(classN, effectiveStrength) combinations of class-enabled parameters.
  const combinations = generateCombinations(classN, effectiveStrength);

  // For each combination, enumerate all class tuples (cartesian product of unique classes).
  for (const combo of combinations) {
    // Get the unique classes for each parameter in this combination.
    const classesPerParam: string[][] = [];
    for (const idx of combo) {
      classesPerParam.push(params[classParams[idx]].uniqueClasses());
    }

    // Compute the number of class tuples for this combination.
    let numTuples = 1;
    for (const cls of classesPerParam) {
      numTuples *= cls.length;
    }

    // Enumerate all class tuples and check coverage.
    for (let flat = 0; flat < numTuples; ++flat) {
      // Decode flat index into class indices.
      const classIndices = new Array<number>(effectiveStrength);
      let remainder = flat;
      for (let i = effectiveStrength - 1; i >= 0; --i) {
        const radix = classesPerParam[i].length;
        classIndices[i] = remainder % radix;
        remainder = Math.trunc(remainder / radix);
      }

      ++report.totalClassTuples;

      // Check if any test case covers this class tuple.
      let covered = false;
      for (const test of tests) {
        let matches = true;
        for (let k = 0; k < effectiveStrength; ++k) {
          const pi = classParams[combo[k]];
          if (pi >= test.values.length) {
            matches = false;
            break;
          }
          const vi = test.values[pi];
          const testClass = params[pi].equivalenceClass(vi);
          if (testClass !== classesPerParam[k][classIndices[k]]) {
            matches = false;
            break;
          }
        }
        if (matches) {
          covered = true;
          break;
        }
      }

      if (covered) {
        ++report.coveredClassTuples;
      }
    }
  }

  if (report.totalClassTuples > 0) {
    report.coverageRatio = report.coveredClassTuples / report.totalClassTuples;
  }

  return report;
}

/**
 * Annotate a GenerateResult with equivalence class coverage if applicable.
 *
 * Checks whether any parameter has equivalence classes defined. If so,
 * computes class coverage and sets the classCoverage field on the result.
 * @param result The generate result to annotate (modified in place).
 * @param params The parameter definitions (with equivalence classes).
 * @param strength The coverage strength used for generation.
 */
export function annotateClassCoverage(
  result: GenerateResult,
  params: Parameter[],
  strength: number,
): void {
  let hasEqClasses = false;
  for (const p of params) {
    if (p.hasEquivalenceClasses) {
      hasEqClasses = true;
      break;
    }
  }
  if (!hasEqClasses) {
    return;
  }

  const classReport = computeClassCoverage(params, result.tests, strength);
  result.classCoverage = {
    totalClassTuples: classReport.totalClassTuples,
    coveredClassTuples: classReport.coveredClassTuples,
    classCoverageRatio: classReport.coverageRatio,
  };
}
