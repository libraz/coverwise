/// @file timing.ts
/// @brief Wall-clock sampling shared by the timing gates in both TypeScript
///        surfaces and the WASM surface.
///
/// Every gate that uses this compares two measurements of the same shape and
/// asserts a ratio, never an absolute duration: only the ratio expresses the
/// property, and only the ratio survives being run on a loaded machine or under
/// coverage instrumentation.

/// Fastest run of each of two workloads, sampling them alternately.
///
/// The floor is what an implementation costs, while the mean also reports
/// whatever else the machine was doing. Timing one workload to completion and
/// only then the other lets a shift in load land wholly on whichever went
/// second, which reads back as a ratio neither workload earned — a cold module
/// on the first run of a session does exactly that. Alternating puts both
/// through the same window, and swapping which one leads on alternate rounds
/// keeps the cost of a round from being charged to the same one every time.
///
/// @param runs Rounds to sample. Every gate here is an upper bound, so raise
///        this until the high side of the ratio stops moving.
/// @returns The fastest `first` and the fastest `second`, in milliseconds.
export function fastestEach(runs: number, first: () => void, second: () => void): [number, number] {
  const timed = (fn: () => void): number => {
    const start = performance.now();
    fn();
    return performance.now() - start;
  };
  let firstBest = Number.POSITIVE_INFINITY;
  let secondBest = Number.POSITIVE_INFINITY;
  for (let i = 0; i < runs; ++i) {
    const firstLeads = i % 2 === 0;
    let firstMs = Number.POSITIVE_INFINITY;
    if (firstLeads) {
      firstMs = timed(first);
    }
    const secondMs = timed(second);
    if (!firstLeads) {
      firstMs = timed(first);
    }
    firstBest = Math.min(firstBest, firstMs);
    secondBest = Math.min(secondBest, secondMs);
  }
  return [firstBest, secondBest];
}
