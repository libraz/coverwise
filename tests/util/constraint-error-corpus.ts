/// @file constraint-error-corpus.ts
/// @brief Reader for the constraint parser corpus shared with the C++ tests.
///
/// The corpus itself lives in constraint_error_corpus.inc, which the C++ tests
/// include directly as an X-macro. Reading the same file here keeps a single
/// list of expressions and expected verdicts for every surface.

import { readFileSync } from 'node:fs';

const CASE_PATTERN =
  /^COVERWISE_CONSTRAINT_CASE\("([^"]*)",\s*(true|false),\s*"([^"]*)",\s*"([^"]*)"\)$/;

/** One expression and the verdict every constraint parser must reach. */
export interface ConstraintCase {
  /** The constraint expression, parsed against the model the corpus declares. */
  expression: string;
  /** Whether the expression is accepted. */
  accepted: boolean;
  /** The error message for a rejection, empty when accepted. */
  message: string;
  /** The error detail for a rejection, empty when accepted. */
  detail: string;
}

function readCorpus(): ConstraintCase[] {
  const source = readFileSync(new URL('./constraint_error_corpus.inc', import.meta.url), 'utf8');
  const cases: ConstraintCase[] = [];
  for (const rawLine of source.split('\n')) {
    const line = rawLine.trim();
    if (line.length === 0 || line.startsWith('//')) {
      continue;
    }
    const match = CASE_PATTERN.exec(line);
    if (match === null) {
      throw new Error(`Unrecognized line in the constraint corpus: ${line}`);
    }
    cases.push({
      expression: match[1],
      accepted: match[2] === 'true',
      message: match[3],
      detail: match[4],
    });
  }
  if (cases.length === 0) {
    throw new Error('The constraint corpus is empty');
  }
  return cases;
}

export const CONSTRAINT_CORPUS: readonly ConstraintCase[] = readCorpus();
