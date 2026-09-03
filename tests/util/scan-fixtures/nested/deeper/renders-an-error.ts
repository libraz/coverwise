// A source that composes the text of an error outside the renderings, placed
// at a depth nothing names. It exists to be found: the scan that guards where
// display text may be built has to reach this file because of where it is, and
// a scan that reached only the directories someone listed would report the tree
// clean while this sat in it.

interface ErrorInfo {
  message: string;
  detail: string;
}

export function describeFailure(error: ErrorInfo): string {
  return [error.message, error.detail].join(': ');
}
