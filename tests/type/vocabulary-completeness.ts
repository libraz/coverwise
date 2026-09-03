/// The vocabulary maps reject what they are supposed to reject.
///
/// A map that names every member of a published shape compiles whether or not
/// it would refuse one that did not, so the map alone is not evidence that a
/// field added to the surface would be caught. Each pairing below hands a map
/// one member short to a place that requires the whole vocabulary.
/// `@ts-expect-error` marks code that must NOT compile, and an unused directive
/// is itself a compile error, so each of these fails in either direction: if a
/// map stopped requiring the member, the assignment would succeed and the
/// directive would go unused.
///
/// `Exclude` names the member to drop rather than restating the map, which is
/// what keeps these from becoming a second vocabulary to maintain — and what
/// makes a member that leaves the union stop being a member these can drop.
///
/// They live apart from the maps because the maps are imported at run time by
/// the tests that read them, and a `declare const` has no value to import.

import type { CoverwiseErrorCode, ParameterValue } from '../../js/index.js';
import type { EveryKey, ParameterField, PublishedInputField } from './public-vocabulary.js';

declare const withoutAnExtendField: EveryKey<Exclude<PublishedInputField, 'mode'>>;
// @ts-expect-error A field the extend input adds is a published input field.
const requiresExtendFields: EveryKey<PublishedInputField> = withoutAnExtendField;

declare const withoutAnOptionalField: EveryKey<Exclude<PublishedInputField, 'strength'>>;
// @ts-expect-error Being optional on the published shape is no excuse for going unnamed.
const requiresOptionalFields: EveryKey<PublishedInputField> = withoutAnOptionalField;

declare const withoutABoundaryField: EveryKey<Exclude<ParameterField, 'step'>>;
// @ts-expect-error A field only one of the parameter shapes declares is still a parameter field.
const requiresBoundaryFields: EveryKey<ParameterField> = withoutABoundaryField;

declare const withoutAValueField: EveryKey<Exclude<keyof ParameterValue, 'aliases'>>;
// @ts-expect-error Every field of the object form of a value has to be named.
const requiresValueFields: EveryKey<keyof ParameterValue> = withoutAValueField;

declare const withoutAnErrorCode: EveryKey<Exclude<CoverwiseErrorCode, 'TUPLE_EXPLOSION'>>;
// @ts-expect-error Every code a caller can branch on has to be named.
const requiresErrorCodes: EveryKey<CoverwiseErrorCode> = withoutAnErrorCode;

void [
  requiresExtendFields,
  requiresOptionalFields,
  requiresBoundaryFields,
  requiresValueFields,
  requiresErrorCodes,
];
