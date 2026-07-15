// npm-check-updates configuration.
//
// TypeScript is pinned to the 6.x line. TypeScript 7 (the native rewrite) ships a
// different package layout, and Yarn's builtin TypeScript compatibility patch
// cannot apply to it (the patch expects `lib/_tsc.js`), so upgrading produces an
// uninstallable lockfile. Everything else tracks the latest release.
//
// Drop the `typescript` special case once the toolchain can install TypeScript 7.
module.exports = {
  target: (packageName) => (packageName === 'typescript' ? 'minor' : 'latest'),
};
