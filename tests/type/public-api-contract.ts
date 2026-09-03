/// Compile-time contract for the shapes a consumer is documented to be able to
/// name and build.
///
/// Which types each entry point publishes is settled by enumeration in the test
/// tier, which compares the export sets themselves rather than a list of names
/// written down somewhere. What is left for a compiler to decide is here: that
/// the published shapes can be used the way the documentation shows, and that
/// the constraint builder cannot offer a chain the grammar cannot parse.
/// `@ts-expect-error` marks the code that must NOT compile — an unused
/// directive is itself a compile error, so this file fails in either direction.

import { when } from '../../js/constraint.js';
import { Coverwise as PureCoverwise } from '../../js/pure/index.js';
import type { Constraint, GenerateResult, IfConstraint, NegativeCoverage } from '../../js/index.js';
import { Coverwise as WasmCoverwise } from '../../js/index.js';

// A typed helper over the negative-testing report is the use case that requires
// NegativeCoverage to be exported rather than reconstructed by hand.
function isNegativeCoverageComplete(result: GenerateResult): boolean {
  const coverage: NegativeCoverage | undefined = result.negativeCoverage;
  return coverage !== undefined && coverage.coverageRatio === 1;
}

void isNegativeCoverageComplete;

// --- Coverwise is created through create() on both entry points ---

declare const wasmInstance: WasmCoverwise;
declare const pureInstance: PureCoverwise;
void [wasmInstance, pureInstance];

// @ts-expect-error Coverwise is constructed through create(), not new.
const directWasm = new WasmCoverwise();
// @ts-expect-error Coverwise is constructed through create(), not new.
const directPure = new PureCoverwise();

void [directWasm, directPure];

// --- The builder cannot construct an ELSE the grammar rejects ---

// The one ELSE the grammar admits, and the types it must produce.
const ifThen: IfConstraint = when('os').eq('mac').then(when('browser').ne('ie'));
const elseBranch: Constraint = ifThen.else(when('arch').ne('arm'));

// Deliberately uninferred by annotation: the negative checks below have to see
// the builder's own return types, not a widened one.
const implied = when('os').eq('mac').implies(when('browser').ne('ie'));
const chained = when('os').eq('mac').then(when('browser').ne('ie')).else(when('arch').ne('arm'));

// @ts-expect-error IMPLIES admits no ELSE branch.
const impliesElse = implied.else(when('arch').ne('arm'));
// @ts-expect-error A second ELSE has no reading in the grammar.
const doubleElse = chained.else(when('arch').eq('x64'));

void [elseBranch, implied, chained, impliesElse, doubleElse];
