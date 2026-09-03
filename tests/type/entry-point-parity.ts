/// Type-level parity contract between the two published entry points.
///
/// `@libraz/coverwise` and `@libraz/coverwise/pure` are advertised as the same
/// API, so a program must type-check after swapping only the import specifier —
/// in either direction. Assigning each module type to the other turns drift in
/// the exported values, their signatures, or their member visibility into a
/// compile error.
///
/// It reaches exactly that far. A type-only export leaves no trace in a
/// module's value type, so nothing here can see the exported types; asserting
/// their parity by naming them would mean a list to extend by hand, and the
/// export sets are compared by enumeration in the test tier instead.

type WasmModule = typeof import('../../js/index.js');
type PureModule = typeof import('../../js/pure/index.js');

declare const wasmExports: WasmModule;
declare const pureExports: PureModule;

const pureAcceptsWasm: PureModule = wasmExports;
const wasmAcceptsPure: WasmModule = pureExports;

void [pureAcceptsWasm, wasmAcceptsPure];

// --- The assignment refuses a pair that has drifted ---
//
// An assignment between two modules that already agree compiles whether or not
// it checks anything, so on its own it is not evidence that drift would be
// caught. Driving it with a pair written to disagree is. `@ts-expect-error`
// marks code that must NOT compile, and an unused directive is itself a compile
// error, so this fails in either direction.

type DriftedWasm = typeof import('../util/entry-fixtures/wasm-entry.js');
type DriftedPure = typeof import('../util/entry-fixtures/pure-entry.js');

declare const driftedWasmExports: DriftedWasm;
declare const driftedPureExports: DriftedPure;

// @ts-expect-error The pair disagrees on what an exported function returns.
const acceptsDrift: DriftedPure = driftedWasmExports;
// @ts-expect-error Drift is reported in both directions, which is what swapping the import needs.
const acceptsDriftBack: DriftedWasm = driftedPureExports;

void [acceptsDrift, acceptsDriftBack];
