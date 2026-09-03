import { beforeAll, describe, expect, it } from 'vitest';
import { init, type Parameter, analyzeCoverage as wasmAnalyzeCoverage } from '../../js/index';
import { analyzeCoverage as pureAnalyzeCoverage } from '../../js/pure/index';
import { fastestEach } from '../util/timing.js';

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

/// Analyzing a suite charges the bytes of every row's key and value against one
/// aggregate budget, so a fixture is bounded by what it spends as well as by
/// what it measures. The two measurements below want different things from that
/// budget — one wants its largest suite to be four times its base, the other
/// wants as many cells as it can get in a single suite — and one pair of
/// constants cannot serve both. They are therefore sized separately, against
/// the same builders and the same assertions.
const PARAM_COUNT = 50;
const VALUES_PER_PARAM = 4;
const BASE_ROWS = 1000;

/// The cross-engine comparison resolves a per-cell cost against the fixed cost
/// of a call, so cells are the whole of what it needs; it builds one suite and
/// never multiplies it. Sized to the widest suite the budget admits.
const CROSS_ENGINE_PARAMS = 200;
const CROSS_ENGINE_ROWS = 900;

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

/// Repetitions per measurement. Chosen by watching the estimator settle rather
/// than by preference: past eight the ratios below stop moving, and every gate
/// here is an upper bound, so what matters is that the high side has converged.
const RUNS = 10;

describe('WASM coverage analysis on a large suite', () => {
  const params = buildParams();
  const ratios = new Map<number, number>();

  beforeAll(async () => {
    await init();
    const base = buildRows(BASE_ROWS);
    // Warm up so the first measured run does not also pay JIT cost.
    wasmAnalyzeCoverage(params, base.slice(0, 100), 2);
    // Each multiple is paired against its own measurement of the base suite, so
    // the two halves of a ratio come from the same stretch of machine time.
    for (const multiple of [2, 4]) {
      const scaled = buildRows(BASE_ROWS * multiple);
      wasmAnalyzeCoverage(params, scaled.slice(0, 100), 2);
      const [baseMs, scaledMs] = fastestEach(
        RUNS,
        () => {
          wasmAnalyzeCoverage(params, base, 2);
        },
        () => {
          wasmAnalyzeCoverage(params, scaled, 2);
        },
      );
      ratios.set(multiple, scaledMs / baseMs);
    }
  }, 600_000);

  it('scales linearly in the row count', { timeout: MEASUREMENT_TIMEOUT_MS }, () => {
    // Converting a row costs the same whatever else is in the suite, so these
    // land on the row multiple itself: 2 and 4. What the bounds separate that
    // from is a per-row cost that grows with the suite, which is quadratic and
    // would put them at 4 and 16.
    //
    // The two are the same property read at two scales, and they are not
    // equally good at it. Doubling separates 2 from 4 and quadrupling separates
    // 4 from 16, so the detection lives in the second one; the first is a
    // near-scale check whose regimes are close enough together that its bound
    // cannot be far from either.
    expect(ratios.get(2) as number).toBeLessThan(3.0);
    expect(ratios.get(4) as number).toBeLessThan(7);
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

    const [narrow, wide] = fastestEach(
      RUNS,
      () => {
        wasmAnalyzeCoverage(narrowParams, narrowRows, 2);
      },
      () => {
        wasmAnalyzeCoverage(wideParams, wideRows, 2);
      },
    );

    // Linear in the model puts this at 2x and a per-cell rescan at 4x. The
    // measured ratio is not 2x but around 2.4, because doubling the model also
    // doubles the strings crossing the JS/WASM boundary per row, and that cost
    // is linear in the model too. The bound is placed above what the linear
    // implementation measures rather than midway between the two ideals, so a
    // loaded machine does not read as a rescan; it stays below 4x, so a rescan
    // still does.
    //
    // That leaves it narrow on both sides — 2.4 measured against 4 for the
    // rescan, with the bound between them and close to each. What holds it is
    // the precision of the sampling above rather than any room in the bound, so
    // a failure here is not answered by raising the number: past 4 the gate
    // detects nothing. It is answered by measuring the conversion against a
    // baseline that does not scale with the model.
    expect(wide / narrow).toBeLessThan(3.5);
  });

  it('stays comparable to the pure TypeScript surface on a wide model', {
    timeout: MEASUREMENT_TIMEOUT_MS,
  }, () => {
    // The documented claim about these two surfaces is that they are comparable
    // on pairwise work, not that the WASM one wins, and what is left after the
    // per-cell conversion cost is one string handed across the JS/WASM boundary
    // per cell — a boundary cost, not an algorithmic one. So this guards the
    // property the default import actually owes its callers: that choosing it
    // is not dramatically worse.
    //
    // One model size, and deliberately so. This is a comparison of two engines
    // at the widest model the file builds, not a reading of how the comparison
    // moves as the model grows: a trend needs two points on that axis, and the
    // second point would have to be separated from host load, which a
    // cross-engine ratio cannot do. Growth in the model is measured by the test
    // above, which compares one engine against itself at two model sizes and so
    // does not inherit the noise of comparing two.
    //
    // The bound is deliberately far from the measured ratio. Which surface
    // leads depends on the host: this comparison runs from 0.6 to 0.9 on a
    // quiet developer machine and has been seen above 1.1 on a shared
    // two-core CI runner, so a bound placed near either figure would be
    // reporting the runner's load rather than the code's behaviour.
    const wideParams = buildParams(CROSS_ENGINE_PARAMS);
    const wideRows = buildRows(CROSS_ENGINE_ROWS, CROSS_ENGINE_PARAMS);

    wasmAnalyzeCoverage(wideParams, wideRows.slice(0, 100), 2);
    pureAnalyzeCoverage(wideParams, wideRows.slice(0, 100), 2);

    const [wasm, pure] = fastestEach(
      RUNS,
      () => {
        wasmAnalyzeCoverage(wideParams, wideRows, 2);
      },
      () => {
        pureAnalyzeCoverage(wideParams, wideRows, 2);
      },
    );

    expect(wasm / pure).toBeLessThan(2);
  });
});
