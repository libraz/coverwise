/**
 * The documented input limits, defined once for every TypeScript surface.
 *
 * This module is the TypeScript mirror of `src/model/limits.h`. The two are
 * kept in lockstep by `limits.test.ts`, which reads the header and asserts each
 * constant matches, so adding a limit on one side without the other fails the
 * suite rather than drifting silently.
 */

/**
 * Maximum number of parameters a single model may declare.
 *
 * The limit is what keeps feasibility search bounded — the search walks one
 * parameter per level, and a satisfying chain spends only one node of the
 * search budget per level, so parameter count is the only thing bounding how
 * deep a search can go.
 */
export const MAX_PARAMETERS = 1024;

/** Maximum number of values a single parameter may declare. */
export const MAX_VALUES_PER_PARAMETER = 16384;

/** Maximum number of rows in a `tests`, `seeds`, or `existing` array. */
export const MAX_TESTS = 100000;

/** Maximum number of constraint expressions in a single model. */
export const MAX_CONSTRAINTS = 256;

/** Maximum UTF-8 byte length of any single input string. */
export const MAX_STRING_BYTES = 64 * 1024;

/** Maximum total UTF-8 byte length of the strings in one input. */
export const MAX_AGGREGATE_STRING_BYTES = 1024 * 1024;

/**
 * Upper bound on the raw bytes of one JSON document a surface reads.
 *
 * This is a memory guard for file/stdin reads, not part of the acceptance
 * contract: the per-entity limits above decide what is accepted. The
 * TypeScript surfaces take already-parsed objects and so never apply it; it is
 * mirrored here only so the two limit lists stay comparable.
 */
export const MAX_DOCUMENT_BYTES = 64 * 1024 * 1024;
