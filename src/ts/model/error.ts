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
