/// The documented string budgets' refusals, worded once for every surface.

import { MAX_AGGREGATE_STRING_BYTES } from './limits.js';

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
