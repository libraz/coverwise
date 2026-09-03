import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { describe, expect, it } from 'vitest';
import { aggregateBudgetExceeded } from './budget.js';

/**
 * The refusal is stated twice — once here, once in C++ — so nothing at build
 * time makes one follow the other. This rebuilds the C++ sentence from its own
 * source: the two quoted fragments of `AggregateBudgetExceededMessage`, in the
 * order it writes them, with the byte figure resolved from `limits.h` where
 * that function reads it from. Comparing the rebuilt sentence to this module's
 * catches a reword on either side, including one that changes only C++ — which
 * comparing two renderings of the same TypeScript constant would not.
 */

const MODEL_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../../model');

/** The C++ definition of the message, as a body of source. */
function messageBody(): string {
  const source = readFileSync(path.join(MODEL_DIR, 'options_validation.cpp'), 'utf8');
  const definition = source.match(
    /std::string AggregateBudgetExceededMessage\(\)\s*\{([\s\S]*?)\n\}/,
  );
  if (definition === null) {
    throw new Error('AggregateBudgetExceededMessage is not defined in options_validation.cpp');
  }
  return definition[1];
}

/** A `size_t` constant from the C++ limits header, by its unqualified name. */
function headerConstant(name: string): number {
  const source = readFileSync(path.join(MODEL_DIR, 'limits.h'), 'utf8');
  const declaration = source.match(new RegExp(`inline constexpr size_t ${name} = ([^;]+);`));
  if (declaration === null) {
    throw new Error(`${name} is not declared in limits.h`);
  }
  return declaration[1]
    .split('*')
    .map((factor) => Number(factor.trim()))
    .reduce((product, factor) => product * factor, 1);
}

/**
 * The sentence the C++ helper returns.
 *
 * Its body is `"..." + std::to_string(<constant>) + "..."`, so the fragments in
 * source order joined around that constant's value are the whole message. The
 * constant is named unqualified there — the function sits inside
 * `namespace model` — and resolving it against the header is exact.
 */
function cppMessage(): string {
  const body = messageBody();
  const fragments = [...body.matchAll(/"((?:[^"\\\n]|\\.)*)"/g)].map((match) => match[1]);
  const constant = body.match(/std::to_string\((\w+)\)/);
  expect(fragments.length, 'quoted fragments in the C++ definition').toBe(2);
  expect(constant, 'the constant the C++ definition interpolates').not.toBeNull();
  return fragments[0] + String(headerConstant((constant as RegExpMatchArray)[1])) + fragments[1];
}

describe('the aggregate budget refusal', () => {
  it('reads the C++ definition', () => {
    expect(messageBody()).toContain('Input strings exceed');
  });

  it('is worded exactly as the C++ model layer words it', () => {
    expect(aggregateBudgetExceeded()).toBe(cppMessage());
  });
});
