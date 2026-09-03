/// The documented string budgets: which strings they charge, and how they refuse.

import { MAX_AGGREGATE_STRING_BYTES, MAX_STRING_BYTES } from './limits.js';

/**
 * The kinds of caller string the documented byte budgets charge.
 *
 * This is the charged set. A string of one of these kinds counts toward both
 * the per-string and the aggregate limit; anything else a caller writes — a
 * field the schema does not read, a row's keys, a number the engine renders
 * itself — counts nowhere. Every surface derives its walk from this list
 * instead of deciding for itself what to count, which is what makes one input
 * cost the same whichever surface reads it. `ChargedString` in
 * src/model/options_validation.h is the C++ half of the same list.
 */
export const CHARGED_STRING_KINDS = [
  'parameterName',
  'parameterValue',
  'valueAlias',
  'equivalenceClass',
  'constraintExpression',
  'subModelParameterName',
  'weightParameterName',
  'weightValueName',
  /**
   * A string a caller wrote in a `tests` / `seeds` / `existing` row. The model
   * layer never sees one: a row reaches it as value indices, so this kind is
   * charged by whichever surface read the row, and by that surface alone.
   */
  'rowValue',
] as const;

/** One member of the charged set. */
export type ChargedStringKind = (typeof CHARGED_STRING_KINDS)[number];

/**
 * What a refusal calls the string it is refusing, one spelling per kind.
 *
 * The spelling belongs to the kind rather than to the surface that caught it,
 * so a caller comparing what two surfaces said about one string is comparing
 * one sentence. The C++ model layer spells them identically, which
 * `budget.test.ts` reads out of its source and checks.
 */
export const chargedStringContext = {
  parameterName: (parameter: string): string => `Parameter name '${parameter}'`,
  parameterValue: (parameter: string, index: number): string => `${parameter}[${index}]`,
  valueAlias: (parameter: string, index: number): string => `Alias at ${parameter}[${index}]`,
  equivalenceClass: (parameter: string, index: number): string => `Class at ${parameter}[${index}]`,
  constraintExpression: (): string => 'Constraint expression',
  subModelParameterName: (): string => 'Sub-model parameter name',
  weightParameterName: (): string => 'Weight parameter name',
  weightValueName: (): string => 'Weight value name',
  rowValue: (field: string, row: number): string => `Value in ${field} row ${row}`,
} satisfies Record<ChargedStringKind, (...args: never[]) => string>;

/**
 * The refusal for a single caller string that is over the per-string limit.
 *
 * @param context What the string is, from {@link chargedStringContext}.
 */
export function stringBudgetExceeded(context: string): string {
  return `${context} exceeds ${MAX_STRING_BYTES} UTF-8 bytes`;
}

/**
 * The refusal for a call whose caller strings exceed the aggregate budget.
 *
 * The budget is a limit on the call, so more than one reader can be the one
 * that crosses it: the wrapper measuring rows, or the model layer measuring the
 * model. They report it in the same words because it is the same limit — a
 * caller comparing what two surfaces told them about one input is comparing the
 * same sentence. The C++ model layer spells it identically, which
 * `budget.test.ts` reads out of its source and checks.
 */
export function aggregateBudgetExceeded(): string {
  return `Input strings exceed ${MAX_AGGREGATE_STRING_BYTES} UTF-8 bytes`;
}
