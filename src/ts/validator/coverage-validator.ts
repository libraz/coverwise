/// Independent coverage validation (does NOT depend on generator/core).

import { type ConstraintNode, ConstraintResult } from '../model/constraint-ast.js';
import { ErrorCode, type ErrorInfo, okError } from '../model/error.js';
import { type Parameter, validateParameters } from '../model/parameter.js';
import {
  type GenerateResult,
  type TestCase,
  UNASSIGNED,
  type UncoveredTuple,
} from '../model/test-case.js';
import { checkedBinomial } from '../util/combinatorics.js';

const MAX_TUPLES = 16_000_000;
const MAX_COMBINATIONS = 1_000_000;
const MAX_DIAGNOSTIC_TUPLES = 1_000;

/** Coverage validation report with human-readable uncovered tuples. */
export interface CoverageReport {
  totalTuples: number;
  coveredTuples: number;
  coverageRatio: number;
  uncovered: UncoveredTuple[];
  uncoveredCount: number;
  omittedUncovered: number;
  invalidTests: Array<{ testIndex: number; reason: string }>;
  error: ErrorInfo;
}

/** Equivalence class coverage report. */
export interface ClassCoverageReport {
  totalClassTuples: number;
  coveredClassTuples: number;
  coverageRatio: number;
  error: ErrorInfo;
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
 * Recursion-node budget for a single feasibility search. Bounds the otherwise
 * exponential backtracking so a hard model terminates; an exhausted budget is
 * reported explicitly rather than being read as "infeasible". Mirrors the C++
 * validator's kMaxSearchNodes.
 */
const MAX_SEARCH_NODES = 2_000_000;

interface SearchBudget {
  remaining: number;
  exceeded: boolean;
}

/**
 * Backtracking stack owned by the caller so a search allocates nothing.
 *
 * `params[i]` / `values[i]` describe the parameter assigned at level `i` and
 * the value currently being tried there. Depth is bounded by the parameter
 * count, so a caller that sizes the stack once can run any number of searches
 * without further allocation.
 */
interface SearchStack {
  params: number[];
  values: number[];
  depth: number;
}

function createSearchStack(size: number): SearchStack {
  return {
    params: new Array<number>(size).fill(0),
    values: new Array<number>(size).fill(0),
    depth: 0,
  };
}

/** Smallest valid value index at or after `start`, or the domain size. */
function nextValidValue(parameter: Parameter, start: number): number {
  for (let vi = start; vi < parameter.size; ++vi) {
    if (!parameter.isInvalid(vi)) {
      return vi;
    }
  }
  return parameter.size;
}

// Independent feasibility oracle. Keep this separate from the generation core
// so cross-surface validation remains an algorithmically independent check.
//
// The search runs on an explicit stack: its depth grows with the parameter
// count, and a satisfiable chain spends only one node of the budget per level,
// so the node budget alone does not bound how deep it goes. Call stack use
// therefore stays independent of the model size, while the enumeration order,
// budget accounting and assignment side effects match a recursive descent.
//
// `assignment` is left exactly as it was received on every exit path, so a
// caller can hand over its own scratch buffer instead of copying the partial.
function validatorSearch(
  params: Parameter[],
  constraints: ConstraintNode[],
  assignment: number[],
  cursor: number,
  budget: SearchBudget,
  stack: SearchStack,
): boolean {
  stack.depth = 0;
  let position = cursor;
  let expand = true;

  for (;;) {
    if (expand) {
      expand = false;
      let dead = false;
      if (budget.remaining === 0) {
        budget.exceeded = true;
        dead = true;
      } else {
        --budget.remaining;
        for (const constraint of constraints) {
          if (constraint.evaluate(assignment) === ConstraintResult.False) {
            dead = true;
            break;
          }
        }
      }
      if (!dead) {
        while (position < params.length && assignment[position] !== UNASSIGNED) {
          ++position;
        }
        if (position === params.length) {
          let satisfied = true;
          for (const constraint of constraints) {
            if (constraint.evaluate(assignment) !== ConstraintResult.True) {
              satisfied = false;
              break;
            }
          }
          if (satisfied) {
            // Undo this search's own writes so the caller's buffer comes back
            // holding exactly the partial assignment it passed in.
            for (let i = 0; i < stack.depth; ++i) {
              assignment[stack.params[i]] = UNASSIGNED;
            }
            return true;
          }
        } else {
          const vi = nextValidValue(params[position], 0);
          if (vi < params[position].size) {
            assignment[position] = vi;
            stack.params[stack.depth] = position;
            stack.values[stack.depth] = vi;
            ++stack.depth;
            ++position;
            expand = true;
            continue;
          }
        }
      }
    }

    // Backtrack. An exhausted budget unwinds without trying further values, so
    // the caller sees an untouched assignment together with budget.exceeded.
    if (stack.depth === 0) {
      return false;
    }
    const top = stack.depth - 1;
    const topParam = stack.params[top];
    const vi = budget.exceeded
      ? params[topParam].size
      : nextValidValue(params[topParam], stack.values[top] + 1);
    if (vi < params[topParam].size) {
      stack.values[top] = vi;
      assignment[topParam] = vi;
      position = topParam + 1;
      expand = true;
      continue;
    }
    assignment[topParam] = UNASSIGNED;
    --stack.depth;
  }
}

/**
 * Whether `partial` extends to a full valid, constraint-satisfying assignment.
 * `partial` is scratch: it is restored before returning.
 */
function hasSatisfyingCompletion(
  params: Parameter[],
  constraints: ConstraintNode[],
  partial: number[],
  stack: SearchStack,
  budget?: SearchBudget,
): boolean {
  const b = budget ?? { remaining: MAX_SEARCH_NODES, exceeded: false };
  return validatorSearch(params, constraints, partial, 0, b, stack);
}

function validateSatisfiableModel(params: Parameter[], constraints: ConstraintNode[]): ErrorInfo {
  if (constraints.length === 0) {
    return okError();
  }
  const budget: SearchBudget = { remaining: MAX_SEARCH_NODES, exceeded: false };
  if (
    hasSatisfyingCompletion(
      params,
      constraints,
      new Array<number>(params.length).fill(UNASSIGNED),
      createSearchStack(params.length),
      budget,
    )
  ) {
    return okError();
  }
  return budget.exceeded
    ? {
        code: ErrorCode.ConstraintError,
        message: 'Constraint search budget exceeded',
        detail: 'The constraint model is too complex to solve within the search budget',
      }
    : {
        code: ErrorCode.ConstraintError,
        message: 'Constraints are unsatisfiable',
        detail: 'No complete assignment using valid values satisfies all constraints',
      };
}

function validatePositiveTest(
  test: TestCase,
  params: Parameter[],
  constraints: ConstraintNode[],
): string {
  if (test.values.length !== params.length) {
    return `expected ${params.length} value(s), got ${test.values.length}`;
  }
  for (let pi = 0; pi < params.length; ++pi) {
    const vi = test.values[pi];
    if (vi === UNASSIGNED) {
      return `missing value for parameter ${params[pi].name}`;
    }
    if (!Number.isInteger(vi) || vi < 0 || vi >= params[pi].size) {
      return `value index ${vi} is out of range for parameter ${params[pi].name}`;
    }
    if (params[pi].isInvalid(vi)) {
      return `value ${params[pi].name}=${params[pi].values[vi]} is marked invalid`;
    }
  }
  for (let ci = 0; ci < constraints.length; ++ci) {
    if (constraints[ci].evaluate(test.values) !== ConstraintResult.True) {
      return `violates constraint #${ci + 1} (constraint evaluation is false or indeterminate)`;
    }
  }
  return '';
}

function preflightEnumeration(params: Parameter[], strength: number): ErrorInfo {
  const n = params.length;
  if (strength === 0 || strength > n) {
    return okError();
  }
  if (checkedBinomial(n, strength, MAX_COMBINATIONS) === null) {
    return {
      code: ErrorCode.TupleExplosion,
      message: 'parameter combination metadata exceeds safety limit',
      detail: `Combinations exceed limit: ${MAX_COMBINATIONS}. Reduce strength or parameter count.`,
    };
  }

  const combo = Array.from({ length: strength }, (_, index) => index);
  let total = 0;
  for (;;) {
    let product = 1;
    for (const pi of combo) {
      product *= params[pi].size;
      if (!Number.isSafeInteger(product) || product > MAX_TUPLES) {
        return {
          code: ErrorCode.TupleExplosion,
          message: 't-wise tuple count exceeds safety limit',
          detail: `Total tuples exceed limit: ${MAX_TUPLES}`,
        };
      }
    }
    total += product;
    if (!Number.isSafeInteger(total) || total > MAX_TUPLES) {
      return {
        code: ErrorCode.TupleExplosion,
        message: 't-wise tuple count exceeds safety limit',
        detail: `Total tuples exceed limit: ${MAX_TUPLES}`,
      };
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
  return okError();
}

/**
 * Independently validate t-wise coverage of a test suite.
 *
 * This validator enumerates all t-tuples from scratch (not using any
 * generator internals) and checks each against the test suite. Tuples
 * containing a value marked invalid are excluded from the coverage universe
 * (they do not count toward totalTuples or uncovered), matching the
 * generator's CoverageEngine.excludeInvalidValues semantics.
 */
export function validateCoverage(
  params: Parameter[],
  tests: TestCase[],
  strength: number,
  constraints: ConstraintNode[] = [],
): CoverageReport {
  const report: CoverageReport = {
    totalTuples: 0,
    coveredTuples: 0,
    coverageRatio: 0,
    uncovered: [],
    uncoveredCount: 0,
    omittedUncovered: 0,
    invalidTests: [],
    error: okError(),
  };

  const n = params.length;

  // A strength of 0, or greater than the parameter count, is invalid input —
  // the same rule generate enforces (generate-options validation). Reporting
  // vacuous 100% coverage here would make the oracle green-light an
  // unanswerable query.
  if (strength === 0 || strength > n) {
    report.error = {
      code: ErrorCode.InvalidInput,
      message: 'Strength must be between 1 and parameter count',
      detail: `strength=${strength}, parameters=${n}`,
    };
    return report;
  }

  // validateParameters enforces the parameter budget (MAX_PARAMETERS), and it
  // runs before any feasibility search: the search walks one parameter per
  // level, so an oversized model has to be rejected up front rather than
  // discovered part-way through a descent.
  const parameterError = validateParameters(params);
  if (parameterError.length > 0) {
    report.error = {
      code: ErrorCode.InvalidInput,
      message: parameterError,
      detail: '',
    };
    return report;
  }

  report.error = validateSatisfiableModel(params, constraints);
  if (report.error.code !== ErrorCode.Ok) {
    return report;
  }

  report.error = preflightEnumeration(params, strength);
  if (report.error.code !== ErrorCode.Ok) {
    return report;
  }

  // Step 1: Generate all C(n, strength) combinations of parameter indices.
  const combinations = generateCombinations(n, strength);

  // Reusable assignment buffer for constraint evaluation.
  const assignment = new Array<number>(n).fill(UNASSIGNED);
  const validTests: TestCase[] = [];
  for (let index = 0; index < tests.length; ++index) {
    const reason = validatePositiveTest(tests[index], params, constraints);
    if (reason.length === 0) {
      validTests.push(tests[index]);
    } else {
      report.invalidTests.push({ testIndex: index, reason });
    }
  }

  // Buffers reused across combinations to avoid per-tuple allocation. The
  // search stack is sized to the maximum depth a search can reach so even the
  // first feasibility check inside the tuple loop cannot grow it.
  const valueIndices = new Array<number>(strength);
  const searchStack = createSearchStack(n);
  let coveredFlags = new Uint8Array(0);

  for (const combo of combinations) {
    // Step 2: Enumerate all value tuples (cartesian product) for this combination.
    let numTuples = 1;
    for (const pi of combo) {
      numTuples *= params[pi].size;
    }

    // Step 2.5: Project every valid test onto its flat value-tuple index for this
    // combination in a single pass, so the coverage check below is an O(1) lookup
    // instead of rescanning all tests per tuple. A valid test always has an
    // in-range value for every parameter (guaranteed by validatePositiveTest), so
    // the projection is total. The encoding here (most-significant digit first)
    // must match the decode in the tuple loop.
    if (coveredFlags.length < numTuples) {
      coveredFlags = new Uint8Array(numTuples);
    } else {
      coveredFlags.fill(0, 0, numTuples);
    }
    for (const test of validTests) {
      let f = 0;
      for (let j = 0; j < strength; ++j) {
        f = f * params[combo[j]].size + test.values[combo[j]];
      }
      coveredFlags[f] = 1;
    }

    // Iterate over all value tuples using a flat index.
    for (let flat = 0; flat < numTuples; ++flat) {
      // Decode flat index into value indices (mixed-radix decomposition).
      let remainder = flat;
      for (let i = strength - 1; i >= 0; --i) {
        const radix = params[combo[i]].size;
        valueIndices[i] = remainder % radix;
        remainder = Math.trunc(remainder / radix);
      }

      // Step 2a: Exclude tuples containing any invalid value from the coverage
      // universe entirely (matches CoverageEngine.excludeInvalidValues). Such
      // tuples do not count toward totalTuples or the uncovered list.
      let containsInvalidValue = false;
      for (let i = 0; i < strength; ++i) {
        if (params[combo[i]].isInvalid(valueIndices[i])) {
          containsInvalidValue = true;
          break;
        }
      }
      if (containsInvalidValue) {
        continue;
      }

      // Step 2b: Exclude constraint-invalid tuples from the universe entirely
      // (matches CoverageEngine.excludeInvalidTuples).
      //
      // A covered tuple needs no search: the valid test that covers it is
      // itself a complete assignment of valid values satisfying every
      // constraint, so the completion witness is already in hand. Asking the
      // solver again can only reproduce that answer — or fail to reach it
      // within the node budget and report a feasible tuple as undecidable.
      const covered = coveredFlags[flat] !== 0;
      if (!covered && constraints.length > 0) {
        for (let i = 0; i < strength; ++i) {
          assignment[combo[i]] = valueIndices[i];
        }
        const tupleBudget: SearchBudget = { remaining: MAX_SEARCH_NODES, exceeded: false };
        const excluded = !hasSatisfyingCompletion(
          params,
          constraints,
          assignment,
          searchStack,
          tupleBudget,
        );
        for (let i = 0; i < strength; ++i) {
          assignment[combo[i]] = UNASSIGNED;
        }
        if (tupleBudget.exceeded) {
          report.error = {
            code: ErrorCode.ConstraintError,
            message: 'Constraint search budget exceeded',
            detail: 'Tuple feasibility could not be determined within the search budget',
          };
          return report;
        }
        if (excluded) {
          continue;
        }
      }

      ++report.totalTuples;

      // Step 3: Coverage is an O(1) lookup into the projection built above.
      if (covered) {
        ++report.coveredTuples;
      } else {
        ++report.uncoveredCount;
        if (report.uncovered.length >= MAX_DIAGNOSTIC_TUPLES) {
          continue;
        }
        // Build the UncoveredTuple with human-readable strings.
        const tuple: string[] = [];
        const paramNames: string[] = [];
        const indices: Array<[number, number]> = [];
        for (let i = 0; i < strength; ++i) {
          const pi = combo[i];
          const vi = valueIndices[i];
          paramNames.push(params[pi].name);
          tuple.push(`${params[pi].name}=${params[pi].values[vi]}`);
          indices.push([pi, vi]);
        }
        report.uncovered.push({
          tuple,
          params: paramNames,
          indices,
          reason: 'never covered',
        });
      }
    }
  }
  report.omittedUncovered = report.uncoveredCount - report.uncovered.length;

  // Compute coverage ratio. When there are no tuples, coverage is vacuously 1.0.
  if (report.totalTuples === 0) {
    report.coverageRatio = 1.0;
  } else {
    report.coverageRatio = report.coveredTuples / report.totalTuples;
  }

  return report;
}

/**
 * Check whether a class tuple has at least one valid, constraint-satisfiable
 * representative value assignment.
 *
 * A class tuple selects one equivalence class per parameter in `comboParamIndices`.
 * A representative picks, for each such parameter, a concrete value whose class
 * matches the required class. The tuple is satisfiable if some choice of
 * representatives uses no invalid value and violates no constraint. Class tuples
 * with no satisfiable representative are excluded from the coverage universe
 * (mirroring the value-level invalid/constraint exclusion).
 */
enum ClassTupleFeasibility {
  Feasible,
  Infeasible,
  BudgetExceeded,
}

/** Marks a value that belongs to no class in a parameter's class domain. */
const NO_CLASS = -1;

interface ClassDomain {
  names: string[];
  validValueIndices: number[][];
  /**
   * Class index per value index, or NO_CLASS. Resolving a value to its class is
   * a flat array read, never a name lookup: the projection below runs it once
   * per (combination, test, position).
   */
  classIndexByValue: number[];
}

function buildClassDomain(parameter: Parameter): ClassDomain {
  const domain: ClassDomain = {
    names: [],
    validValueIndices: [],
    classIndexByValue: new Array<number>(parameter.size).fill(NO_CLASS),
  };
  // Name lookup is confined to this one-off build; nothing downstream hashes.
  const indexByName = new Map<string, number>();
  for (let vi = 0; vi < parameter.size; ++vi) {
    if (parameter.isInvalid(vi)) {
      continue;
    }
    const className = parameter.equivalenceClass(vi);
    if (className.length === 0) {
      continue;
    }
    let classIndex = indexByName.get(className);
    if (classIndex === undefined) {
      classIndex = domain.names.length;
      indexByName.set(className, classIndex);
      domain.names.push(className);
      domain.validValueIndices.push([]);
    }
    domain.validValueIndices[classIndex].push(vi);
    domain.classIndexByValue[vi] = classIndex;
  }
  return domain;
}

function preflightClassEnumeration(domains: ClassDomain[], strength: number): ErrorInfo {
  const n = domains.length;
  if (checkedBinomial(n, strength, MAX_COMBINATIONS) === null) {
    return {
      code: ErrorCode.TupleExplosion,
      message: 'class combination metadata exceeds safety limit',
      detail: `Combinations exceed limit: ${MAX_COMBINATIONS}`,
    };
  }
  const combo = Array.from({ length: strength }, (_, index) => index);
  let total = 0;
  for (;;) {
    let product = 1;
    for (const index of combo) {
      product *= domains[index].names.length;
      if (!Number.isSafeInteger(product) || product > MAX_TUPLES) {
        return {
          code: ErrorCode.TupleExplosion,
          message: 'equivalence-class tuple count exceeds safety limit',
          detail: `Total class tuples exceed limit: ${MAX_TUPLES}`,
        };
      }
    }
    total += product;
    if (!Number.isSafeInteger(total) || total > MAX_TUPLES) {
      return {
        code: ErrorCode.TupleExplosion,
        message: 'equivalence-class tuple count exceeds safety limit',
        detail: `Total class tuples exceed limit: ${MAX_TUPLES}`,
      };
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
  return okError();
}

function classTupleHasValidRepresentative(
  params: Parameter[],
  comboParamIndices: number[],
  candidates: readonly (readonly number[])[],
  constraints: ConstraintNode[],
  assignment: number[],
  choice: number[],
  stack: SearchStack,
): ClassTupleFeasibility {
  const k = comboParamIndices.length;
  choice.length = k;
  choice.fill(0);
  // One representative exhausting its budget says nothing about the others, so
  // it must not end the search: only a feasible representative, or the whole
  // enumeration completing, decides the tuple. An exhausted budget is remembered
  // and reported only when no representative proved feasible, which keeps the
  // verdict independent of the order values appear in.
  let anyExceeded = false;
  for (;;) {
    for (let i = 0; i < k; ++i) {
      assignment[comboParamIndices[i]] = candidates[i][choice[i]];
    }
    const budget: SearchBudget = { remaining: MAX_SEARCH_NODES, exceeded: false };
    const violated = !hasSatisfyingCompletion(params, constraints, assignment, stack, budget);
    for (let i = 0; i < k; ++i) {
      assignment[comboParamIndices[i]] = UNASSIGNED;
    }
    if (!violated) {
      return ClassTupleFeasibility.Feasible;
    }
    if (budget.exceeded) {
      anyExceeded = true;
    }

    // Advance the mixed-radix choice vector.
    let pos = k - 1;
    while (pos >= 0) {
      if (++choice[pos] < candidates[pos].length) {
        break;
      }
      choice[pos] = 0;
      --pos;
    }
    if (pos < 0) {
      break;
    }
  }
  return anyExceeded ? ClassTupleFeasibility.BudgetExceeded : ClassTupleFeasibility.Infeasible;
}

/**
 * Compute equivalence class coverage for a test suite.
 *
 * Maps each value to its equivalence class and enumerates all t-wise class
 * tuples, counting how many are covered by the test suite.
 * Only considers parameters that have equivalence classes defined.
 *
 * A class tuple is included in the universe only if it has at least one
 * representative value tuple that contains no invalid value and satisfies all
 * constraints, so a fully valid-covering suite is not penalized with
 * classCoverageRatio < 1.0.
 * @param constraints Optional constraints threaded into class-tuple enumeration.
 * @returns Class coverage report. An empty class universe — strength outside
 *          [1, parameter count], no parameter carrying equivalence classes, or
 *          every class tuple excluded as infeasible — reports zero counts with
 *          coverageRatio 1.0 and an ok error, so a suite is never penalized for
 *          a universe with nothing to cover. coverageRatio is left at 0 only on
 *          an error exit (invalid parameters, unsatisfiable constraints, an
 *          exceeded enumeration limit, or an exhausted feasibility budget),
 *          which is signalled by a non-ok error and where the counts are
 *          partial. Detect "no classes" via the counts and error, never via
 *          coverageRatio === 0.
 */
export function computeClassCoverage(
  params: Parameter[],
  tests: TestCase[],
  strength: number,
  constraints: ConstraintNode[] = [],
): ClassCoverageReport {
  const report: ClassCoverageReport = {
    totalClassTuples: 0,
    coveredClassTuples: 0,
    coverageRatio: 0,
    error: okError(),
  };

  const n = params.length;

  // A universe with no class tuples to cover is vacuously fully covered (1.0),
  // matching the totalClassTuples === 0 branch below and validateCoverage's
  // empty-universe handling. Only a genuine enumeration error keeps ratio 0.
  if (strength === 0 || strength > n) {
    report.coverageRatio = 1.0;
    return report;
  }

  const parameterError = validateParameters(params);
  if (parameterError.length > 0) {
    report.error = {
      code: ErrorCode.InvalidInput,
      message: parameterError,
      detail: '',
    };
    return report;
  }

  report.error = validateSatisfiableModel(params, constraints);
  if (report.error.code !== ErrorCode.Ok) {
    return report;
  }

  // Identify parameters that have equivalence classes.
  const classParams: number[] = [];
  const classDomains: ClassDomain[] = [];
  for (let i = 0; i < n; ++i) {
    if (params[i].hasEquivalenceClasses) {
      classParams.push(i);
      classDomains.push(buildClassDomain(params[i]));
    }
  }

  if (classParams.length === 0) {
    report.coverageRatio = 1.0;
    return report;
  }

  // For class coverage we consider combinations of parameters that have classes.
  // If fewer parameters have classes than the strength, use the available count.
  const classN = classParams.length;
  const effectiveStrength = Math.min(strength, classN);

  report.error = preflightClassEnumeration(classDomains, effectiveStrength);
  if (report.error.code !== ErrorCode.Ok) {
    return report;
  }

  // Generate all C(classN, effectiveStrength) combinations of class-enabled parameters.
  const combinations = generateCombinations(classN, effectiveStrength);
  const validTests = tests.filter(
    (test) => validatePositiveTest(test, params, constraints).length === 0,
  );

  const classIndices = new Array<number>(effectiveStrength);
  const comboParamIndices = new Array<number>(effectiveStrength);
  const requiredCandidates = new Array<readonly number[]>(effectiveStrength);
  const assignment = new Array<number>(params.length).fill(UNASSIGNED);
  const choice: number[] = [];
  const searchStack = createSearchStack(params.length);
  let coveredFlags = new Uint8Array(0);

  // For each combination, enumerate all class tuples.
  for (const combo of combinations) {
    // Resolve the global parameter indices for this class combination once.
    for (let k = 0; k < effectiveStrength; ++k) {
      comboParamIndices[k] = classParams[combo[k]];
    }

    // Compute the number of class tuples for this combination.
    let numTuples = 1;
    for (const index of combo) {
      numTuples *= classDomains[index].names.length;
    }

    if (coveredFlags.length < numTuples) {
      coveredFlags = new Uint8Array(numTuples);
    } else {
      coveredFlags.fill(0, 0, numTuples);
    }
    for (const test of validTests) {
      let projected = 0;
      let inDomain = true;
      for (let k = 0; k < effectiveStrength; ++k) {
        const domain = classDomains[combo[k]];
        const classIndex = domain.classIndexByValue[test.values[classParams[combo[k]]]];
        if (classIndex === NO_CLASS) {
          inDomain = false;
          break;
        }
        projected = projected * domain.names.length + classIndex;
      }
      if (inDomain) {
        coveredFlags[projected] = 1;
      }
    }

    // Enumerate all class tuples and check coverage.
    for (let flat = 0; flat < numTuples; ++flat) {
      // Decode flat index into class indices.
      let remainder = flat;
      for (let i = effectiveStrength - 1; i >= 0; --i) {
        const radix = classDomains[combo[i]].validValueIndices.length;
        classIndices[i] = remainder % radix;
        remainder = Math.trunc(remainder / radix);
      }

      if (constraints.length > 0) {
        for (let k = 0; k < effectiveStrength; ++k) {
          requiredCandidates[k] = classDomains[combo[k]].validValueIndices[classIndices[k]];
        }
        const feasibility = classTupleHasValidRepresentative(
          params,
          comboParamIndices,
          requiredCandidates,
          constraints,
          assignment,
          choice,
          searchStack,
        );
        if (feasibility === ClassTupleFeasibility.BudgetExceeded) {
          report.error = {
            code: ErrorCode.ConstraintError,
            message: 'Constraint search budget exceeded',
            detail: 'Class-tuple feasibility could not be determined within the search budget',
          };
          return report;
        }
        if (feasibility === ClassTupleFeasibility.Infeasible) {
          continue;
        }
      }

      ++report.totalClassTuples;
      if (coveredFlags[flat]) {
        ++report.coveredClassTuples;
      }
    }
  }

  if (report.totalClassTuples > 0) {
    report.coverageRatio = report.coveredClassTuples / report.totalClassTuples;
  } else {
    report.coverageRatio = 1.0;
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
 * @param constraints Optional constraints threaded into class-tuple enumeration.
 */
export function annotateClassCoverage(
  result: GenerateResult,
  params: Parameter[],
  strength: number,
  constraints: ConstraintNode[] = [],
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

  const classReport = computeClassCoverage(params, result.tests, strength, constraints);
  if (classReport.error.code !== ErrorCode.Ok) {
    if (result.error.code === ErrorCode.Ok) {
      result.error = classReport.error;
    }
    result.warnings.push(`${classReport.error.message}: ${classReport.error.detail}`);
    return;
  }
  result.classCoverage = {
    totalClassTuples: classReport.totalClassTuples,
    coveredClassTuples: classReport.coveredClassTuples,
    classCoverageRatio: classReport.coverageRatio,
  };
}
