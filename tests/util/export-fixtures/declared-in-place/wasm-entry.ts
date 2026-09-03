// A published type declared in the entry point itself.
//
// Its counterpart declares a `Handle` of its own with a different shape. Both
// entry points publish the same names, out of no shared module, so a comparison
// that only enumerated re-exports would see nothing here at all.

export interface Handle {
  close(): void;
}

export type { Report } from './vocabulary.js';
