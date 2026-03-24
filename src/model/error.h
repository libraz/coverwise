/// @file error.h
/// @brief Structured error type with error codes and context.

#ifndef COVERWISE_MODEL_ERROR_H_
#define COVERWISE_MODEL_ERROR_H_

#include <cstdint>
#include <string>

namespace coverwise {
namespace model {

/// @brief Structured error with context.
struct Error {
  enum class Code {
    kOk = 0,
    kConstraintError = 1,
    kInsufficientCoverage = 2,
    kInvalidInput = 3,
    kTupleExplosion = 4,
  };

  Code code = Code::kOk;
  std::string message;
  std::string detail;

  bool ok() const { return code == Code::kOk; }
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_ERROR_H_
