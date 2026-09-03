/// Structured error type with error codes and context.

/** Error codes matching exit codes. */
export enum ErrorCode {
  Ok = 0,
  ConstraintError = 1,
  InsufficientCoverage = 2,
  InvalidInput = 3,
  TupleExplosion = 4,
}

/** Structured error with context. */
export interface ErrorInfo {
  code: ErrorCode;
  message: string;
  detail: string;
}

/** Create a successful (no-error) ErrorInfo. */
export function okError(): ErrorInfo {
  return { code: ErrorCode.Ok, message: '', detail: '' };
}

/** Check if an ErrorInfo represents success. */
export function isOk(error: ErrorInfo): boolean {
  return error.code === ErrorCode.Ok;
}

/**
 * The one representation of a failure that reaches a caller as text.
 *
 * Mirrors `model::SurfaceError` in the C++ core, and lives beside ErrorInfo for
 * the same reason it lives in `model/` there: every layer that produces an
 * error is allowed to depend on the model layer, so this is the one rendering
 * all of them can reach. The absent-detail case is decided here, so no call
 * site can leave a dangling ": " behind.
 */
export function surfaceErrorText(error: ErrorInfo): string {
  return error.detail ? `${error.message}: ${error.detail}` : error.message;
}
