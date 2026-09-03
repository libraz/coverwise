/// Compile-time contract for the parameter shapes a consumer writes by hand.
///
/// The boundary fields are an all-or-nothing union, and which combinations are
/// admissible is stated in the types rather than checked at run time — so what
/// keeps that statement honest is a file that must NOT compile in places.
/// `@ts-expect-error` marks each of those, and an unused directive is itself a
/// compile error, so every negative below fails in either direction: the shape
/// it names has to stay refused, and the refusal has to stay for that reason.

import type { Parameter, ParameterValue } from '../../js/types.js';

const plain: Parameter = { name: 'mode', values: ['fast', 'safe'] };
const integer: Parameter = { name: 'port', values: [80], type: 'integer', range: [1, 65535] };
const floating: Parameter = {
  name: 'ratio',
  values: [0.5],
  type: 'float',
  range: [0, 1],
  step: 0.1,
};

// @ts-expect-error Boundary type requires a range.
const missingRange: Parameter = { name: 'port', values: [80], type: 'integer' };
// @ts-expect-error Boundary range requires a type.
const missingType: Parameter = { name: 'port', values: [80], range: [1, 65535] };
// @ts-expect-error Integer boundaries accept only step 1.
const invalidIntegerStep: Parameter = {
  name: 'port',
  values: [80],
  type: 'integer',
  range: [1, 65535],
  step: 2,
};

// Each negative below is written on one line: `@ts-expect-error` reaches the
// line that follows it, and a literal broken across lines can report its error
// on an inner line the directive never covered.

// @ts-expect-error A range is an interval, not a list of any length.
const shortRange: Parameter = { name: 'p', values: [80], type: 'integer', range: [1] };
// @ts-expect-error A range is an interval, not a list of any length.
const longRange: Parameter = { name: 'p', values: [80], type: 'integer', range: [1, 2, 3] };
// @ts-expect-error Only the two documented boundary kinds exist.
const unknownBoundaryKind: Parameter = { name: 'p', values: [80], type: 'decimal', range: [1, 2] };
// @ts-expect-error A plain parameter is the shape with no boundary fields at all.
const plainWithStep: Parameter = { name: 'mode', values: ['fast'], step: 1 };

const value: ParameterValue = { value: 'fast', invalid: false, aliases: ['f'], class: 'quick' };
// @ts-expect-error A value object carries the documented fields and no others.
const valueWithUnknownField: ParameterValue = { value: 'fast', weight: 2 };

void [
  plain,
  integer,
  floating,
  missingRange,
  missingType,
  invalidIntegerStep,
  shortRange,
  longRange,
  unknownBoundaryKind,
  plainWithStep,
  value,
  valueWithUnknownField,
];
