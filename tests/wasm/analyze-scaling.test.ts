import { beforeAll, describe, expect, it } from 'vitest';
import { init, type Parameter, analyzeCoverage as wasmAnalyzeCoverage } from '../../js/index';
import { analyzeCoverage as pureAnalyzeCoverage } from '../../js/pure/index';

/// Argument conversion cost of the WASM surface on a large recorded suite.
///
/// Converting a suite is the whole cost of analyzing one: the model is small and
/// fixed while the row count is not, so a per-row cost that is anything worse
/// than linear in the model size decides how the surface behaves at the
/// documented input ceiling. These are wall-clock comparisons of the same work
/// on the same inputs rather than absolute budgets, so they hold on any machine.

/// These tests measure wall-clock ratios over repeated runs of a large suite, and
/// CI runs them under v8 coverage instrumentation on a shared runner. A per-test
/// timeout sized for ordinary unit tests does not apply, so each one states its
/// own; it is a ceiling on a hang, not a performance budget. The assertions below
/// compare two measurements from the same run, so they are unaffected by it.
const MEASUREMENT_TIMEOUT_MS = 120_000;

const PARAM_COUNT = 100;
const VALUES_PER_PARAM = 4;
const BASE_ROWS = 2000;

function buildParams(paramCount: number = PARAM_COUNT): Parameter[] {
  return Array.from({ length: paramCount }, (_, i) => ({
    name: `p${i}`,
    values: Array.from({ length: VALUES_PER_PARAM }, (_, v) => `v${v}`),
  }));
}

function buildRows(count: number, paramCount: number = PARAM_COUNT): Array<Record<string, string>> {
  const rows: Array<Record<string, string>> = [];
  for (let r = 0; r < count; ++r) {
    const row: Record<string, string> = {};
    for (let i = 0; i < paramCount; ++i) {
      row[`p${i}`] = `v${(r * 7 + i * 3) % VALUES_PER_PARAM}`;
    }
    rows.push(row);
  }
  return rows;
}

/// Fastest of several runs: the floor is what the implementation costs, while
/// the mean also reports whatever else the machine was doing.
function fastest(runs: number, fn: () => void): number {
  let best = Number.POSITIVE_INFINITY;
  for (let i = 0; i < runs; ++i) {
    const start = performance.now();
    fn();
    best = Math.min(best, performance.now() - start);
  }
  return best;
}

describe('WASM coverage analysis on a large suite', () => {
  const params = buildParams();
  const timings = new Map<number, number>();

  beforeAll(async () => {
    await init();
    for (const rowCount of [BASE_ROWS, BASE_ROWS * 2, BASE_ROWS * 4]) {
      const rows = buildRows(rowCount);
      // Warm up so the first measured run does not also pay JIT cost.
      wasmAnalyzeCoverage(params, rows.slice(0, 100), 2);
      timings.set(
        rowCount,
        fastest(3, () => {
          wasmAnalyzeCoverage(params, rows, 2);
        }),
      );
    }
  }, 600_000);

  it('scales linearly in the row count', { timeout: MEASUREMENT_TIMEOUT_MS }, () => {
    const base = timings.get(BASE_ROWS) as number;
    const doubled = timings.get(BASE_ROWS * 2) as number;
    const quadrupled = timings.get(BASE_ROWS * 4) as number;

    // Linear scaling puts these at 2x and 4x. The allowance absorbs measurement
    // noise while still failing a per-row cost that grows with the suite.
    expect(doubled / base).toBeLessThan(3.5);
    expect(quadrupled / base).toBeLessThan(7);
  });

  // Row count is the wrong axis for the cost this guards. Resolving a row's keys
  // once cost a scan of every parameter, so conversion was quadratic in the model
  // and linear in the rows: doubling rows doubled the work either way. Doubling
  // the parameter count is what separates the two, because it doubles the cells
  // and, if each cell rescans the model, doubles the per-cell cost as well.
  it('converts a row in time proportional to the model, not its square', {
    timeout: MEASUREMENT_TIMEOUT_MS,
  }, () => {
    const narrowParams = buildParams(PARAM_COUNT);
    const wideParams = buildParams(PARAM_COUNT * 2);
    const narrowRows = buildRows(BASE_ROWS, PARAM_COUNT);
    const wideRows = buildRows(BASE_ROWS, PARAM_COUNT * 2);

    wasmAnalyzeCoverage(narrowParams, narrowRows.slice(0, 100), 2);
    wasmAnalyzeCoverage(wideParams, wideRows.slice(0, 100), 2);

    const narrow = fastest(5, () => {
      wasmAnalyzeCoverage(narrowParams, narrowRows, 2);
    });
    const wide = fastest(5, () => {
      wasmAnalyzeCoverage(wideParams, wideRows, 2);
    });

    // Linear in the model puts this at 2x and a per-cell rescan at 4x. The
    // measured ratio is not 2x but around 2.4, because doubling the model also
    // doubles the strings crossing the JS/WASM boundary per row, and that cost
    // is linear in the model too. The bound is placed above what the linear
    // implementation measures rather than midway between the two ideals, so a
    // loaded machine does not read as a rescan; it stays below 4x, so a rescan
    // still does.
    expect(wide / narrow).toBeLessThan(3.5);
  });

  it('stays comparable to the pure TypeScript surface as the model grows', {
    timeout: MEASUREMENT_TIMEOUT_MS,
  }, () => {
    // The documented claim about these two surfaces is that they are comparable
    // on pairwise work, not that the WASM one wins, and what is left after the
    // per-cell conversion cost is one string handed across the JS/WASM boundary
    // per cell — a boundary cost, not an algorithmic one. So this guards the
    // property the default import actually owes its callers: that choosing it
    // is not dramatically worse. A per-cell cost growing with the model is
    // caught by the test above instead, which measures one engine against
    // itself and so does not inherit the noise of comparing two.
    //
    // The bound is deliberately far from the measured ratio. Which surface
    // leads depends on the host: this comparison runs from 0.6 to 0.9 on a
    // quiet developer machine and has been seen above 1.1 on a shared
    // two-core CI runner, so a bound placed near either figure would be
    // reporting the runner's load rather than the code's behaviour.
    const wideParams = buildParams(PARAM_COUNT * 2);
    const wideRows = buildRows(BASE_ROWS, PARAM_COUNT * 2);

    wasmAnalyzeCoverage(wideParams, wideRows.slice(0, 100), 2);
    pureAnalyzeCoverage(wideParams, wideRows.slice(0, 100), 2);

    const wasm = fastest(5, () => {
      wasmAnalyzeCoverage(wideParams, wideRows, 2);
    });
    const pure = fastest(5, () => {
      pureAnalyzeCoverage(wideParams, wideRows, 2);
    });

    expect(wasm / pure).toBeLessThan(2);
  });
});
