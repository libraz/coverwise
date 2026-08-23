/// Compile-time contract for the shapes a consumer is documented to be able to
/// name and build.
///
/// Every named interface that appears in a documented return shape must be
/// importable by name from both entry points, and the constraint builder must
/// not offer a chain the grammar cannot parse. `@ts-expect-error` marks the code
/// that must NOT compile — an unused directive is itself a compile error, so
/// this file fails in either direction.

import { when } from '../../js/constraint.js';
import type {
  BoundaryParameter as PureBoundaryParameter,
  ClassCoverage as PureClassCoverage,
  Condition as PureCondition,
  ConditionStart as PureConditionStart,
  Constraint as PureConstraint,
  CoverageReport as PureCoverageReport,
  CoverwiseErrorCode as PureCoverwiseErrorCode,
  ExtendInput as PureExtendInput,
  FloatBoundaryParameter as PureFloatBoundaryParameter,
  GenerateInput as PureGenerateInput,
  GenerateResult as PureGenerateResult,
  GenerateStats as PureGenerateStats,
  IfConstraint as PureIfConstraint,
  IntegerBoundaryParameter as PureIntegerBoundaryParameter,
  ModelStats as PureModelStats,
  NegativeCoverage as PureNegativeCoverage,
  Parameter as PureParameter,
  ParameterValue as PureParameterValue,
  ParamStats as PureParamStats,
  PlainParameter as PurePlainParameter,
  SubModel as PureSubModel,
  Suggestion as PureSuggestion,
  TestCase as PureTestCase,
  UncoveredTuple as PureUncoveredTuple,
  WeightConfig as PureWeightConfig,
} from '../../js/pure/index.js';
import { Coverwise as PureCoverwise } from '../../js/pure/index.js';
import type {
  BoundaryParameter,
  ClassCoverage,
  Condition,
  ConditionStart,
  Constraint,
  CoverageReport,
  CoverwiseErrorCode,
  ExtendInput,
  FloatBoundaryParameter,
  GenerateInput,
  GenerateResult,
  GenerateStats,
  IfConstraint,
  IntegerBoundaryParameter,
  ModelStats,
  NegativeCoverage,
  Parameter,
  ParameterValue,
  ParamStats,
  PlainParameter,
  SubModel,
  Suggestion,
  TestCase,
  UncoveredTuple,
  WeightConfig,
} from '../../js/index.js';
import { Coverwise as WasmCoverwise } from '../../js/index.js';

// --- Every documented public type is nameable from both entry points ---

declare const wasmTypes: [
  BoundaryParameter,
  ClassCoverage,
  Condition,
  ConditionStart,
  Constraint,
  CoverageReport,
  CoverwiseErrorCode,
  ExtendInput,
  FloatBoundaryParameter,
  GenerateInput,
  GenerateResult,
  GenerateStats,
  IfConstraint,
  IntegerBoundaryParameter,
  ModelStats,
  NegativeCoverage,
  Parameter,
  ParameterValue,
  ParamStats,
  PlainParameter,
  SubModel,
  Suggestion,
  TestCase,
  UncoveredTuple,
  WeightConfig,
];

declare const pureTypes: [
  PureBoundaryParameter,
  PureClassCoverage,
  PureCondition,
  PureConditionStart,
  PureConstraint,
  PureCoverageReport,
  PureCoverwiseErrorCode,
  PureExtendInput,
  PureFloatBoundaryParameter,
  PureGenerateInput,
  PureGenerateResult,
  PureGenerateStats,
  PureIfConstraint,
  PureIntegerBoundaryParameter,
  PureModelStats,
  PureNegativeCoverage,
  PureParameter,
  PureParameterValue,
  PureParamStats,
  PurePlainParameter,
  PureSubModel,
  PureSuggestion,
  PureTestCase,
  PureUncoveredTuple,
  PureWeightConfig,
];

void [wasmTypes, pureTypes];

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
