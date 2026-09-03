// A vocabulary module with a type neither entry point of this pair passes on,
// and one that only the wasm side passes on.

export interface Report {
  coverage: number;
}

export interface Unpublished {
  reachableFromNeitherEntryPoint: true;
}
