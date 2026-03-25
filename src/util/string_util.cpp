/// @file string_util.cpp

#include "util/string_util.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>

namespace coverwise {
namespace util {

bool CaseInsensitiveEqual(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

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
