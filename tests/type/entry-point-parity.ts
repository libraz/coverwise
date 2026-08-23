/// Type-level parity contract between the two published entry points.
///
/// `@libraz/coverwise` and `@libraz/coverwise/pure` are advertised as the same
/// API, so a program must type-check after swapping only the import specifier —
/// in either direction. Assigning each module type to the other turns drift in
/// the exported names, signatures, or member visibility into a compile error,
/// and the per-name checks below do the same for the exported types, which a
/// module-type assignment alone does not reach.

import type * as Pure from '../../js/pure/index.js';
import type * as Wasm from '../../js/index.js';

type WasmModule = typeof import('../../js/index.js');
type PureModule = typeof import('../../js/pure/index.js');

declare const wasmExports: WasmModule;
declare const pureExports: PureModule;

const pureAcceptsWasm: PureModule = wasmExports;
const wasmAcceptsPure: WasmModule = pureExports;

void [pureAcceptsWasm, wasmAcceptsPure];

/** Structural identity, strict enough to catch optional/readonly drift. */
type Equal<A, B> = (<T>() => T extends A ? 1 : 2) extends <T>() => T extends B ? 1 : 2
  ? true
  : false;

type Assert<T extends true> = T;

// Every type either entry point exports by name. A name dropped from one of the
// two re-export lists fails to resolve here.
type TypeParity = [
  Assert<Equal<Wasm.BoundaryParameter, Pure.BoundaryParameter>>,
  Assert<Equal<Wasm.ClassCoverage, Pure.ClassCoverage>>,
  Assert<Equal<Wasm.Condition, Pure.Condition>>,
  Assert<Equal<Wasm.ConditionStart, Pure.ConditionStart>>,
  Assert<Equal<Wasm.Constraint, Pure.Constraint>>,
  Assert<Equal<Wasm.CoverageReport, Pure.CoverageReport>>,
  Assert<Equal<Wasm.Coverwise, Pure.Coverwise>>,
  Assert<Equal<WasmModule['Coverwise'], PureModule['Coverwise']>>,
  Assert<Equal<Wasm.CoverwiseError, Pure.CoverwiseError>>,
  Assert<Equal<Wasm.CoverwiseErrorCode, Pure.CoverwiseErrorCode>>,
  Assert<Equal<Wasm.ExtendInput, Pure.ExtendInput>>,
  Assert<Equal<Wasm.FloatBoundaryParameter, Pure.FloatBoundaryParameter>>,
  Assert<Equal<Wasm.GenerateInput, Pure.GenerateInput>>,
  Assert<Equal<Wasm.GenerateResult, Pure.GenerateResult>>,
  Assert<Equal<Wasm.GenerateStats, Pure.GenerateStats>>,
  Assert<Equal<Wasm.IfConstraint, Pure.IfConstraint>>,
  Assert<Equal<Wasm.IntegerBoundaryParameter, Pure.IntegerBoundaryParameter>>,
  Assert<Equal<Wasm.ModelStats, Pure.ModelStats>>,
  Assert<Equal<Wasm.NegativeCoverage, Pure.NegativeCoverage>>,
  Assert<Equal<Wasm.Parameter, Pure.Parameter>>,
  Assert<Equal<Wasm.ParameterValue, Pure.ParameterValue>>,
  Assert<Equal<Wasm.ParamStats, Pure.ParamStats>>,
  Assert<Equal<Wasm.PlainParameter, Pure.PlainParameter>>,
  Assert<Equal<Wasm.SubModel, Pure.SubModel>>,
  Assert<Equal<Wasm.Suggestion, Pure.Suggestion>>,
  Assert<Equal<Wasm.TestCase, Pure.TestCase>>,
  Assert<Equal<Wasm.UncoveredTuple, Pure.UncoveredTuple>>,
  Assert<Equal<Wasm.WeightConfig, Pure.WeightConfig>>,
];

declare const typeParity: TypeParity;
void typeParity;
