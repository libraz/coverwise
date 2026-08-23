#include "util/combinatorics.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <type_traits>

namespace coverwise {
namespace util {
namespace {

// The budget type carries half of the cross-language guarantee, and it carries
// it at compile time: a budget wider than 2^32 - 1 is the range where this
// implementation and the TypeScript port can reach different verdicts, so it
// must be impossible to name rather than merely unused. Deleting only some of
// the wider types would leave the rest to narrow silently, so pin the whole set
// here instead of trusting the declaration to have covered it.
static_assert(std::is_constructible_v<BinomialLimit, uint32_t>,
              "a 32-bit budget is the domain both implementations agree on");
static_assert(!std::is_convertible_v<uint32_t, BinomialLimit>,
              "a budget is spelled out at the call site, never conjured from a bare count");
static_assert(!std::is_constructible_v<BinomialLimit, uint64_t>,
              "a 64-bit budget must not narrow into the agreed domain");
static_assert(!std::is_constructible_v<BinomialLimit, int64_t>, "signed 64-bit is no different");
static_assert(!std::is_constructible_v<BinomialLimit, unsigned long long>,
              "the widest unsigned type is refused whatever uint64_t happens to alias");
static_assert(!std::is_constructible_v<BinomialLimit, long long>,
              "the widest signed type is refused whatever int64_t happens to alias");
static_assert(!std::is_constructible_v<BinomialLimit, double>,
              "a floating-point budget carries values no integer domain has");
static_assert(!std::is_constructible_v<BinomialLimit, float>, "and neither does a narrower one");

// Signed types are refused at the near end for the same reason wide ones are
// refused at the far end: a negative budget would convert to 4294967295 and
// silently become the most permissive budget there is, which is the opposite of
// what whoever wrote it meant. A literal budget carries a u suffix instead.
static_assert(!std::is_constructible_v<BinomialLimit, int>,
              "a negative budget must not be expressible");
static_assert(!std::is_constructible_v<BinomialLimit, long>, "nor through a wider signed type");

TEST(DecodeMixedRadixTest, BinaryRadix) {
  std::vector<uint32_t> radixes = {2, 2, 2};
  std::vector<uint32_t> out(3);

  DecodeMixedRadix(0, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{0, 0, 0}));

  DecodeMixedRadix(5, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{1, 0, 1}));

  DecodeMixedRadix(7, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{1, 1, 1}));
}

TEST(DecodeMixedRadixTest, MixedRadix) {
  std::vector<uint32_t> radixes = {3, 2, 4};
  std::vector<uint32_t> out(3);

  DecodeMixedRadix(0, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{0, 0, 0}));

  DecodeMixedRadix(1, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{0, 0, 1}));

  DecodeMixedRadix(4, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{0, 1, 0}));

  DecodeMixedRadix(23, radixes, out);
  EXPECT_EQ(out, (std::vector<uint32_t>{2, 1, 3}));
}

TEST(DecodeMixedRadixTest, SingleElement) {
  std::vector<uint32_t> radixes = {5};
  std::vector<uint32_t> out(1);

  DecodeMixedRadix(3, radixes, out);
  EXPECT_EQ(out[0], 3u);
}

TEST(GenerateCombinationsFlatTest, MatchesLexicographicNestedOrder) {
  EXPECT_EQ(GenerateCombinationsFlat(4, 2),
            (std::vector<uint32_t>{0, 1, 0, 2, 0, 3, 1, 2, 1, 3, 2, 3}));
}

// CheckedBinomial guards five allocation-size checks, so an exact value matters
// as much as the accept/reject verdict: a value that is off by one still sizes a
// buffer, and a limit that is honoured one step too late still allocates it.
TEST(CheckedBinomialTest, ReturnsExactValuesForSmallInputs) {
  uint64_t result = 12345;

  EXPECT_TRUE(CheckedBinomial(5, 2, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 10u);

  EXPECT_TRUE(CheckedBinomial(5, 5, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(5, 0, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(0, 0, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(1, 1, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(6, 3, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 20u);

  EXPECT_TRUE(CheckedBinomial(20, 2, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 190u);

  EXPECT_TRUE(CheckedBinomial(52, 5, BinomialLimit(5'000'000u), result));
  EXPECT_EQ(result, 2'598'960u);
}

// k > n is a legal query, not an error: the caller asks for a combination count
// that happens to be zero and must be able to size a zero-length buffer from it.
TEST(CheckedBinomialTest, ReportsZeroForKGreaterThanN) {
  uint64_t result = 12345;

  EXPECT_TRUE(CheckedBinomial(3, 5, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 0u);

  EXPECT_TRUE(CheckedBinomial(0, 1, BinomialLimit(1000u), result));
  EXPECT_EQ(result, 0u);

  // Even a zero limit accepts it — nothing would be allocated.
  EXPECT_TRUE(CheckedBinomial(3, 5, BinomialLimit(0u), result));
  EXPECT_EQ(result, 0u);
}

TEST(CheckedBinomialTest, AcceptsAtTheLimitAndRejectsOneStepPast) {
  uint64_t result = 0;

  EXPECT_TRUE(CheckedBinomial(5, 2, BinomialLimit(10u), result));
  EXPECT_EQ(result, 10u);

  EXPECT_FALSE(CheckedBinomial(5, 2, BinomialLimit(9u), result));

  // The shipped call sites use a one-million combination budget; C(1000, 2) is
  // just under it and C(1415, 2) just over.
  EXPECT_TRUE(CheckedBinomial(1000, 2, BinomialLimit(1'000'000u), result));
  EXPECT_EQ(result, 499'500u);

  EXPECT_FALSE(CheckedBinomial(1415, 2, BinomialLimit(1'000'000u), result));
}

// Every count past the widest budget is refused for exceeding it, and the
// running product never wraps on the way there: the budget caps the previous
// factor at 2^32 - 1 and n caps the numerator at the same value, so the largest
// product the domain admits is (2^32 - 2) * (2^32 - 1), one step short of 2^64.
// A wrapped product would land back under the budget and be accepted.
TEST(CheckedBinomialTest, RefusesCountsPastTheWidestBudgetWithoutWrapping) {
  uint64_t result = 0;
  const BinomialLimit widest(std::numeric_limits<uint32_t>::max());

  EXPECT_FALSE(CheckedBinomial(std::numeric_limits<uint32_t>::max(), 10, widest, result));
  EXPECT_FALSE(CheckedBinomial(200, 100, widest, result));
  EXPECT_FALSE(CheckedBinomial(64, 32, widest, result));
  EXPECT_FALSE(CheckedBinomial(4'000'000'000u, 2, widest, result));

  // The query that reaches that largest product.
  EXPECT_FALSE(CheckedBinomial(std::numeric_limits<uint32_t>::max(), 2, widest, result));

  // A count landing exactly on the widest budget is still accepted, so the
  // refusals above are the budget speaking and not an early bail-out.
  EXPECT_TRUE(CheckedBinomial(std::numeric_limits<uint32_t>::max(), 1, widest, result));
  EXPECT_EQ(result, 4'294'967'295ull);
}

// Every entry of the corpus the TypeScript port drives as well. The two
// implementations bound their arithmetic differently, so agreement over the
// whole budget range any call site uses has to be asserted, not assumed.
TEST(CheckedBinomialTest, MatchesTheSharedCorpus) {
#define COVERWISE_BINOMIAL_CASE(n, k, limit, accepted, value)                          \
  {                                                                                    \
    uint64_t result = 0xdeadbeef;                                                      \
    const bool ok = CheckedBinomial(n##u, k##u, BinomialLimit(limit##u), result);      \
    EXPECT_EQ(ok, accepted) << "C(" << n##u << ", " << k##u << ") limit " << limit##u; \
    if (accepted) {                                                                    \
      EXPECT_EQ(result, value##ull) << "C(" << n##u << ", " << k##u << ")";            \
    }                                                                                  \
  }
#include "binomial_corpus.inc"
#undef COVERWISE_BINOMIAL_CASE
}

// The intermediate result stays exact through the division: computing the whole
// numerator first would overflow long before C(34, 17) is reached, and that is
// the largest count of its family the widest budget still accepts.
TEST(CheckedBinomialTest, KeepsLargeCountsExactUnderTheWidestBudget) {
  uint64_t result = 0;
  const BinomialLimit widest(std::numeric_limits<uint32_t>::max());

  EXPECT_TRUE(CheckedBinomial(34, 17, widest, result));
  EXPECT_EQ(result, 2'333'606'220ull);

  EXPECT_FALSE(CheckedBinomial(35, 17, widest, result));
}

}  // namespace
}  // namespace util
}  // namespace coverwise
