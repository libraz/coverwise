// Golden corpus for the checked binomial coefficient, shared by the C++ and
// TypeScript tests.
//
// CheckedBinomial (C++) and checkedBinomial (TypeScript) guard the same
// allocation-size checks on their respective surfaces, but they reach their
// verdict through different arithmetic: C++ refuses a multiplication that would
// wrap 64-bit unsigned arithmetic, TypeScript refuses a result that leaves the
// double safe-integer range. Every limit here is at most 2^32 - 1, the largest
// budget any shipped call site passes; over that domain the two mechanisms must
// return the same verdict and the same exact value for every input.
//
// Each entry is COVERWISE_BINOMIAL_CASE(n, k, limit, accepted, value), where
// `value` is the exact C(n, k) when accepted and 0 otherwise.
//
// C++ consumers define COVERWISE_BINOMIAL_CASE before including this file; the
// TypeScript consumers read it with the regular expression in
// tests/util/binomial-corpus.ts.

// Small exact values.
COVERWISE_BINOMIAL_CASE(5, 2, 1000, true, 10)
COVERWISE_BINOMIAL_CASE(6, 3, 1000, true, 20)
COVERWISE_BINOMIAL_CASE(20, 2, 1000, true, 190)
COVERWISE_BINOMIAL_CASE(52, 5, 5000000, true, 2598960)

// C(n, n) and C(n, 0) are both 1, and the limit never applies to them because
// the product loop runs zero times.
COVERWISE_BINOMIAL_CASE(5, 5, 1000, true, 1)
COVERWISE_BINOMIAL_CASE(5, 0, 1000, true, 1)
COVERWISE_BINOMIAL_CASE(5, 0, 0, true, 1)
COVERWISE_BINOMIAL_CASE(1, 1, 1000, true, 1)
COVERWISE_BINOMIAL_CASE(0, 0, 1000, true, 1)

// k > n is a legal query answered with zero, not a rejection: the caller sizes
// an empty buffer from it, so even a zero limit accepts it.
COVERWISE_BINOMIAL_CASE(3, 5, 1000, true, 0)
COVERWISE_BINOMIAL_CASE(0, 1, 1000, true, 0)
COVERWISE_BINOMIAL_CASE(3, 5, 0, true, 0)

// Just under and just over the limit.
COVERWISE_BINOMIAL_CASE(5, 2, 10, true, 10)
COVERWISE_BINOMIAL_CASE(5, 2, 9, false, 0)
COVERWISE_BINOMIAL_CASE(1414, 2, 1000000, true, 998991)
COVERWISE_BINOMIAL_CASE(1415, 2, 1000000, false, 0)
COVERWISE_BINOMIAL_CASE(30, 15, 1000000, false, 0)

// The widest budget a shipped call site passes, at its own boundary.
COVERWISE_BINOMIAL_CASE(65535, 2, 4294967295, true, 2147385345)
COVERWISE_BINOMIAL_CASE(92682, 2, 4294967295, true, 4294930221)
COVERWISE_BINOMIAL_CASE(92683, 2, 4294967295, false, 0)

// Inputs whose exact value is far past any 32-bit budget. The last two would
// also overflow 64-bit arithmetic, so both mechanisms have to refuse them before
// a wrapped product lands back under the limit.
COVERWISE_BINOMIAL_CASE(100, 50, 4294967295, false, 0)
COVERWISE_BINOMIAL_CASE(4000000000, 2, 4294967295, false, 0)
COVERWISE_BINOMIAL_CASE(4294967295, 10, 4294967295, false, 0)
