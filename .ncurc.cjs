// npm-check-updates configuration.
//
// Two packages are held below their latest release; everything else tracks latest.
//
// - typescript: pinned to the 6.x line. TypeScript 7 (the native rewrite) no longer
//   exports the compiler API from the `typescript` package entry — it resolves to a
//   version-only module — and the export-parity and caller-code tests parse TypeScript
//   sources with that API. Drop the pin once those tests have another parser.
//
// - @types/node: pinned to the 22.x line so the ambient types match the Node runtime
//   pinned in mise.toml. A higher major declares APIs that runtime does not have.
//   Raise both together.
const PINNED_TO_MINOR = new Set(['typescript', '@types/node']);

module.exports = {
  target: (packageName) => (PINNED_TO_MINOR.has(packageName) ? 'minor' : 'latest'),
};
