import { describe, expect, it } from 'vitest';
import { ErrorCode, isOk, okError } from './error.js';

describe('ErrorCode', () => {
  it('has expected enum values', () => {
    expect(ErrorCode.Ok).toBe(0);
    expect(ErrorCode.ConstraintError).toBe(1);
    expect(ErrorCode.InsufficientCoverage).toBe(2);
    expect(ErrorCode.InvalidInput).toBe(3);
    expect(ErrorCode.TupleExplosion).toBe(4);
  });
});

describe('okError', () => {
  it('returns an ErrorInfo with code Ok', () => {
    const err = okError();
    expect(err.code).toBe(ErrorCode.Ok);
    expect(err.message).toBe('');
    expect(err.detail).toBe('');
  });
});

describe('isOk', () => {
  it('returns true for an ok error', () => {
    expect(isOk(okError())).toBe(true);
  });

  it('returns false for a non-ok error', () => {
    expect(isOk({ code: ErrorCode.InvalidInput, message: 'bad', detail: '' })).toBe(false);
    expect(isOk({ code: ErrorCode.ConstraintError, message: '', detail: '' })).toBe(false);
  });
});
