// The other declaration of `Report`. A caller who swapped the import specifier
// would find `coverage` gone and a field of another type in its place, while
// every published name still matched.

export interface Report {
  coverageRatio: string;
}
