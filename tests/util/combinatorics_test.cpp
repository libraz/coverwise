#include "util/combinatorics.h"

#include <gtest/gtest.h>

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

}  // namespace
}  // namespace util
}  // namespace coverwise
