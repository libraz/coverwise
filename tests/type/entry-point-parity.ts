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
