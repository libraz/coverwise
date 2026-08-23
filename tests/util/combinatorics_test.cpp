#include "util/combinatorics.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

namespace coverwise {
namespace util {
namespace {

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

  EXPECT_TRUE(CheckedBinomial(5, 2, 1000, result));
  EXPECT_EQ(result, 10u);

  EXPECT_TRUE(CheckedBinomial(5, 5, 1000, result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(5, 0, 1000, result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(0, 0, 1000, result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(1, 1, 1000, result));
  EXPECT_EQ(result, 1u);

  EXPECT_TRUE(CheckedBinomial(6, 3, 1000, result));
  EXPECT_EQ(result, 20u);

  EXPECT_TRUE(CheckedBinomial(20, 2, 1000, result));
  EXPECT_EQ(result, 190u);

  EXPECT_TRUE(CheckedBinomial(52, 5, 5'000'000, result));
  EXPECT_EQ(result, 2'598'960u);
}

// k > n is a legal query, not an error: the caller asks for a combination count
// that happens to be zero and must be able to size a zero-length buffer from it.
TEST(CheckedBinomialTest, ReportsZeroForKGreaterThanN) {
  uint64_t result = 12345;

  EXPECT_TRUE(CheckedBinomial(3, 5, 1000, result));
  EXPECT_EQ(result, 0u);

  EXPECT_TRUE(CheckedBinomial(0, 1, 1000, result));
  EXPECT_EQ(result, 0u);

  // Even a zero limit accepts it — nothing would be allocated.
  EXPECT_TRUE(CheckedBinomial(3, 5, 0, result));
  EXPECT_EQ(result, 0u);
}

TEST(CheckedBinomialTest, AcceptsAtTheLimitAndRejectsOneStepPast) {
  uint64_t result = 0;

  EXPECT_TRUE(CheckedBinomial(5, 2, 10, result));
  EXPECT_EQ(result, 10u);

  EXPECT_FALSE(CheckedBinomial(5, 2, 9, result));

  // The shipped call sites use a one-million combination budget; C(1000, 2) is
  // just under it and C(1415, 2) just over.
  EXPECT_TRUE(CheckedBinomial(1000, 2, 1'000'000, result));
  EXPECT_EQ(result, 499'500u);

  EXPECT_FALSE(CheckedBinomial(1415, 2, 1'000'000, result));
}

// The overflow guard has to fire before the multiplication wraps, not after: a
// wrapped product would land back under the limit and be accepted.
TEST(CheckedBinomialTest, RejectsCountsThatWouldOverflowSixtyFourBitArithmetic) {
  uint64_t result = 0;

  EXPECT_FALSE(CheckedBinomial(std::numeric_limits<uint32_t>::max(), 10,
                               std::numeric_limits<uint64_t>::max(), result));
  EXPECT_FALSE(CheckedBinomial(200, 100, std::numeric_limits<uint64_t>::max(), result));

  // The guard is conservative by design: C(64, 32) fits in a uint64_t, but the
  // running product on the way there does not, so it is refused rather than
  // wrapped. C(62, 31) is the largest of this family the exact path reaches.
  EXPECT_FALSE(CheckedBinomial(63, 31, std::numeric_limits<uint64_t>::max(), result));
  EXPECT_FALSE(CheckedBinomial(64, 32, std::numeric_limits<uint64_t>::max(), result));
}

// A count far past a uint32 limit is refused for exceeding the limit, not for
// overflowing: raising the limit to the full uint64 range accepts the same
// query and yields the exact value.
TEST(CheckedBinomialTest, SeparatesTheLimitVerdictFromTheOverflowVerdict) {
  uint64_t result = 0;

  EXPECT_FALSE(CheckedBinomial(4'000'000'000u, 2, std::numeric_limits<uint32_t>::max(), result));

  EXPECT_TRUE(CheckedBinomial(4'000'000'000u, 2, std::numeric_limits<uint64_t>::max(), result));
  EXPECT_EQ(result, 7'999'999'998'000'000'000ull);
}

// Every entry of the corpus the TypeScript port drives as well. The two
// implementations bound their arithmetic differently, so agreement over the
// whole budget range any call site uses has to be asserted, not assumed.
TEST(CheckedBinomialTest, MatchesTheSharedCorpus) {
#define COVERWISE_BINOMIAL_CASE(n, k, limit, accepted, value)                      \
  {                                                                                \
    uint64_t result = 0xdeadbeef;                                                  \
    const bool ok = CheckedBinomial(n##u, k##u, limit##ull, result);               \
    EXPECT_EQ(ok, accepted) << "C(" << n << ", " << k << ") limit " << limit##ull; \
    if (accepted) {                                                                \
      EXPECT_EQ(result, value##ull) << "C(" << n << ", " << k << ")";              \
    }                                                                              \
  }
#include "binomial_corpus.inc"
#undef COVERWISE_BINOMIAL_CASE
}

// The intermediate result stays exact through the division: computing the whole
// numerator first would overflow long before C(62, 31) is reached.
TEST(CheckedBinomialTest, KeepsLargeCountsExactBelowTheOverflowThreshold) {
  uint64_t result = 0;

  EXPECT_TRUE(CheckedBinomial(60, 30, std::numeric_limits<uint64_t>::max(), result));
  EXPECT_EQ(result, 118'264'581'564'861'424ull);

  EXPECT_TRUE(CheckedBinomial(62, 31, std::numeric_limits<uint64_t>::max(), result));
  EXPECT_EQ(result, 465'428'353'255'261'088ull);
}

}  // namespace
}  // namespace util
}  // namespace coverwise
