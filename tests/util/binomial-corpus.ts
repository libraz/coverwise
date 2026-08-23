/// @file binomial-corpus.ts
/// @brief Reader for the checked binomial corpus shared with the C++ tests.
///
/// The corpus itself lives in binomial_corpus.inc, which the C++ tests include
/// directly as an X-macro. Reading the same file here keeps one list of inputs
/// and expected verdicts for both implementations.

import { readFileSync } from 'node:fs';

const CASE_PATTERN =
  /^COVERWISE_BINOMIAL_CASE\((\d+),\s*(\d+),\s*(\d+),\s*(true|false),\s*(\d+)\)$/;

/** One (n, k, limit) query and the verdict every implementation must reach. */
export interface BinomialCase {
  n: number;
  k: number;
  limit: number;
  /** Whether the implementation reports a usable value for this query. */
  accepted: boolean;
  /** The exact C(n, k) when accepted, 0 otherwise. */
  value: number;
}

function readCorpus(): BinomialCase[] {
  const source = readFileSync(new URL('./binomial_corpus.inc', import.meta.url), 'utf8');
  const cases: BinomialCase[] = [];
  for (const rawLine of source.split('\n')) {
    const line = rawLine.trim();
    if (!line.startsWith('COVERWISE_BINOMIAL_CASE')) {
      continue;
    }
    const match = CASE_PATTERN.exec(line);
    if (!match) {
      throw new Error(`unparsable binomial corpus entry: ${line}`);
    }
    cases.push({
      n: Number(match[1]),
      k: Number(match[2]),
      limit: Number(match[3]),
      accepted: match[4] === 'true',
      value: Number(match[5]),
    });
  }
  if (cases.length === 0) {
    throw new Error('binomial corpus is empty');
  }
  return cases;
}

export const BINOMIAL_CORPUS: readonly BinomialCase[] = readCorpus();
