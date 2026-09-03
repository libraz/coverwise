/// @file class-tuple-fixture.ts
/// @brief The model that discriminates the class-tuple representative-order
///        hazard, shared by the TypeScript surfaces that exercise it.
///
/// Two layers ask the same question of this model — the validator directly
/// through class annotation, and the engine through a whole generate run — and
/// what makes it discriminating is a balance between the constraint expression,
/// the value counts per parameter and the filler domain sizes. A copy per layer
/// can be tuned on one side and leave the other testing a model that no longer
/// separates anything, so the data lives here once and both consume it by
/// reference.

/// A parameter of the fixture model, in the plain shape both layers accept.
export interface RepresentativeOrderParameterSpec {
  name: string;
  values: string[];
  equivalenceClasses?: string[];
  invalid?: boolean[];
}

/// Expression whose only cheap witnesses are gate="open" and pick="cheap". With
/// both fixed the other way it stays undecided until "relief" is assigned, and
/// "none" — its only satisfying value — is invalid, so proving the branch
/// unsatisfiable costs more than the search budget allows. Every filler
/// parameter has more valid values than "relief", so a search ordering
/// parameters by ascending domain size settles the branch immediately while one
/// walking parameters in declaration order does not.
export const COSTLY_REPRESENTATIVE_EXPRESSION = 'gate="open" OR pick="cheap" OR relief="none"';

/// Build a model whose "same" class holds one cheap and one costly
/// representative. `cheapFirst` places the cheap representative at value index 0
/// when true and at value index 1 when false; the class tuple is feasible either
/// way, so both orders must produce the same verdict.
export function representativeOrderParameters(
  cheapFirst: boolean,
): RepresentativeOrderParameterSpec[] {
  return [
    {
      name: 'gate',
      values: ['open', 'shut'],
      equivalenceClasses: ['open_class', 'shut_class'],
    },
    {
      name: 'pick',
      values: cheapFirst ? ['cheap', 'costly'] : ['costly', 'cheap'],
      equivalenceClasses: ['same', 'same'],
    },
    ...Array.from({ length: 14 }, (_, index) => ({
      name: `f${index}`,
      values: ['a', 'b', 'c'],
    })),
    { name: 'relief', values: ['r0', 'r1', 'none'], invalid: [false, false, true] },
  ];
}
