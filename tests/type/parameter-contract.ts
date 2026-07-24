import type { Parameter } from '../../js/types.js';

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

void [plain, integer, floating, missingRange, missingType, invalidIntegerStep];
