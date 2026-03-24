#include "model/parameter.h"

namespace coverwise {
namespace model {

uint32_t Parameter::valid_count() const {
  if (invalid_.empty()) return size();
  uint32_t count = 0;
  for (uint32_t i = 0; i < size(); ++i) {
    if (!is_invalid(i)) ++count;
  }
  return count;
}

uint32_t Parameter::invalid_count() const { return size() - valid_count(); }

bool Parameter::is_invalid(uint32_t index) const {
  if (invalid_.empty()) return false;
  if (index >= static_cast<uint32_t>(invalid_.size())) return false;
  return invalid_[index];
}

bool Parameter::has_invalid_values() const {
  for (size_t i = 0; i < invalid_.size(); ++i) {
    if (invalid_[i]) return true;
  }
  return false;
}

}  // namespace model
}  // namespace coverwise
