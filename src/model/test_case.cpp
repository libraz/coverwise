/// @file test_case.cpp

#include "model/test_case.h"

namespace coverwise {
namespace model {

std::string UncoveredTuple::ToString() const {
  std::string result;
  for (size_t i = 0; i < tuple.size(); ++i) {
    if (i > 0) {
      result += ", ";
    }
    result += tuple[i];
  }
  return result;
}

}  // namespace model
}  // namespace coverwise
