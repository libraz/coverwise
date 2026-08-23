/// @file numeric-parse-corpus.ts
/// @brief Reader for the decimal parsing corpus shared with the C++ tests.
///
/// The corpus itself lives in numeric_parse_corpus.inc, which the C++ tests
/// include directly as an X-macro. Reading the same file here keeps a single
/// list of decimals and expected doubles for every surface.

import { readFileSync } from 'node:fs';

const CASE_PATTERN =
  /^COVERWISE_NUMERIC_CASE\("([^"]*)",\s*0x([0-9a-fA-F]{16})ull,\s*k(Accept|Reject)Literal\)$/;

/** One decimal and the double every surface must parse it into. */
export interface NumericParseCase {
  /** The decimal as it appears in a model value or a constraint literal. */
  text: string;
  /** IEEE-754 bit pattern of Number(text). */
  bits: bigint;
  /** Whether a relational constraint literal spelled this way is accepted. */
  acceptsLiteral: boolean;
}

/** Reinterpret a double as its IEEE-754 bit pattern. */
export function doubleToBits(value: number): bigint {
  const view = new DataView(new ArrayBuffer(8));
  view.setFloat64(0, value);
  return view.getBigUint64(0);
}

/** Render a bit pattern the way the corpus file spells it. */
export function bitsToHex(bits: bigint): string {
  return `0x${bits.toString(16).padStart(16, '0')}`;
}

function readCorpus(): NumericParseCase[] {
  const source = readFileSync(new URL('./numeric_parse_corpus.inc', import.meta.url), 'utf8');
  const cases: NumericParseCase[] = [];
  for (const line of source.split('\n')) {
    const trimmed = line.trim();
    if (trimmed.length === 0 || trimmed.startsWith('//')) {
      continue;
    }
    const match = trimmed.match(CASE_PATTERN);
    if (match === null) {
      throw new Error(`Unrecognized line in the numeric parse corpus: ${trimmed}`);
    }
    cases.push({
      text: match[1],
      bits: BigInt(`0x${match[2]}`),
      acceptsLiteral: match[3] === 'Accept',
    });
  }
  if (cases.length === 0) {
    throw new Error('The numeric parse corpus is empty');
  }
  return cases;
}

export const NUMERIC_PARSE_CORPUS: readonly NumericParseCase[] = readCorpus();
