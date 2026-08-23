// Golden corpus for the checked binomial coefficient, shared by the C++ and
// TypeScript tests.
//
// CheckedBinomial (C++) and checkedBinomial (TypeScript) guard the same
// allocation-size checks on their respective surfaces, but they reach their
// verdict through different arithmetic: C++ refuses a multiplication that would
// wrap 64-bit unsigned arithmetic, TypeScript refuses a result that leaves the
// double safe-integer range. Every limit here is at most 2^32 - 1, which is also
// the widest budget either implementation accepts: C++ cannot construct a wider
// one and TypeScript throws on it. Over that domain the two mechanisms must
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

// The widest budget either implementation accepts, at its own boundary.
COVERWISE_BINOMIAL_CASE(65535, 2, 4294967295, true, 2147385345)
COVERWISE_BINOMIAL_CASE(92682, 2, 4294967295, true, 4294930221)
COVERWISE_BINOMIAL_CASE(92683, 2, 4294967295, false, 0)

// A count that lands exactly on the widest budget, one just under it, and the
// same count one step past a budget that no longer admits it. C(n, 1) is the
// only shape that reaches 2^32 - 1 exactly, so it is what pins the boundary
// itself rather than the neighbourhood around it.
COVERWISE_BINOMIAL_CASE(4294967294, 1, 4294967295, true, 4294967294)
COVERWISE_BINOMIAL_CASE(4294967295, 1, 4294967295, true, 4294967295)
COVERWISE_BINOMIAL_CASE(4294967295, 1, 4294967294, false, 0)

// The same boundary reached through the k -> min(k, n - k) reduction: the loop
// still runs once, so a k that would take 4 billion iterations does not.
COVERWISE_BINOMIAL_CASE(4294967295, 4294967294, 4294967295, true, 4294967295)

// The largest running product the widest budget admits: the previous factor is
// capped at 2^32 - 1 by the budget and the numerator at 2^32 - 1 by n, so the
// multiplication reaches (2^32 - 2) * (2^32 - 1) and stops just short of 2^64.
// C++ must reach the budget verdict without its overflow guard firing, and
// TypeScript must refuse the same query for leaving the safe-integer range.
COVERWISE_BINOMIAL_CASE(4294967295, 2, 4294967295, false, 0)

// Counts whose exact value depends on dividing after every multiplication:
// forming the whole numerator first would overflow long before either is
// reached. C(34, 17) is the largest of this family that fits the widest budget
// and C(35, 17) the smallest that does not.
COVERWISE_BINOMIAL_CASE(34, 17, 4294967295, true, 2333606220)
COVERWISE_BINOMIAL_CASE(35, 17, 4294967295, false, 0)

// Inputs whose exact value is far past any 32-bit budget. The last two would
// also overflow 64-bit arithmetic, so both mechanisms have to refuse them before
// a wrapped product lands back under the limit.
COVERWISE_BINOMIAL_CASE(100, 50, 4294967295, false, 0)
COVERWISE_BINOMIAL_CASE(4000000000, 2, 4294967295, false, 0)
COVERWISE_BINOMIAL_CASE(4294967295, 10, 4294967295, false, 0)
