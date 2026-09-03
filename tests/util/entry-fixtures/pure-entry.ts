export interface Report {
  coverage: number;
}

export function summarize(report: Report): number {
  return report.coverage;
}
