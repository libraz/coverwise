/// @file string_util.cpp

#include "util/string_util.h"

#include <cerrno>
#include <cstdlib>

namespace coverwise {
namespace util {

bool IsNumeric(const std::string& s) {
  if (s.empty()) return false;
  char* end = nullptr;
  errno = 0;
  std::strtod(s.c_str(), &end);
  return errno == 0 && end != nullptr && *end == '\0';
}

double ToDouble(const std::string& s) { return std::strtod(s.c_str(), nullptr); }

}  // namespace util
}  // namespace coverwise
