// One half of an entry-point pair that has drifted in a value signature.
//
// Its counterpart exports the same names with a parameter type of its own, so a
// program written against one of them does not type-check against the other.
// The pair exists to be refused: it is what shows the module-type assignment
// still reports drift rather than accepting anything put in front of it.

export interface Report {
  coverage: number;
}

export function summarize(report: Report): string {
  return String(report.coverage);
}
