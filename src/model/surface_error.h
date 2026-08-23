/// @file surface_error.h
/// @brief The surface-facing representation of a failure.

#ifndef COVERWISE_MODEL_SURFACE_ERROR_H_
#define COVERWISE_MODEL_SURFACE_ERROR_H_

#include <string>

#include "model/error.h"

namespace coverwise {
namespace model {

/// @brief Everything a user is shown about a failure, derived from one Error.
///
/// A process exit code, a stderr line, a warning string, the message carried by
/// a thrown JS or Python exception: each surface reads the field it needs off
/// this type, and this type has exactly one constructor, taking a model::Error.
/// There is no constructor from an exit code and none from a message, so an
/// exit code that disagrees with the failure that produced it, or a message
/// assembled by hand at the call site, is not expressible rather than merely
/// discouraged. Consolidating this into a function has not held: three previous
/// passes left a function named as the single mapping and a caller that bypassed
/// it.
///
/// The empty-detail case is normalized once, at construction, which is what
/// keeps a dangling ": " out of every representation.
class SurfaceError {
 public:
  explicit SurfaceError(const Error& error)
      : code_(error.code), exit_code_(ExitCodeFor(error.code)), text_(Compose(error)) {}

  /// @brief The structured code of the failure this was built from.
  Error::Code code() const { return code_; }

  /// @brief The documented process exit code, for command-line surfaces.
  ///
  /// Total over Error::Code: constraint errors are 1, insufficient coverage is
  /// 2, invalid input and tuple explosion are 3, ok is 0. The raw enum value is
  /// never used as an exit code — kTupleExplosion is 4, which is not one of the
  /// documented exit codes.
  int exit_code() const { return exit_code_; }

  /// @brief `message` on its own, or `message: detail` when a detail is present.
  const std::string& text() const { return text_; }

 private:
  static int ExitCodeFor(Error::Code code) {
    switch (code) {
      case Error::Code::kConstraintError:
        return 1;
      case Error::Code::kInsufficientCoverage:
        return 2;
      case Error::Code::kInvalidInput:
      case Error::Code::kTupleExplosion:
        return 3;
      case Error::Code::kOk:
        return 0;
    }
    return 0;
  }

  static std::string Compose(const Error& error) {
    if (error.detail.empty()) return error.message;
    return error.message + ": " + error.detail;
  }

  Error::Code code_;
  int exit_code_;
  std::string text_;
};

/// @brief How a command-line operation finished.
///
/// Either Success(), or a SurfaceError — and a SurfaceError only exists if a
/// model::Error produced it. There is no constructor taking an int, so a
/// command cannot return an exit code of its own: `return 3;` does not compile,
/// which is what keeps a subcommand from quietly disagreeing with the mapping
/// while a comment nearby claims they all share it.
class ExitStatus {
 public:
  /// @brief The one status that is not a failure.
  static ExitStatus Success() { return ExitStatus(0); }

  /// @brief Deliberately not explicit: an operation returns the surfaced
  /// failure directly, which is what makes the failure the only way to name a
  /// non-zero status.
  ExitStatus(const SurfaceError& surfaced) : exit_code_(surfaced.exit_code()) {}

  int exit_code() const { return exit_code_; }

 private:
  explicit ExitStatus(int exit_code) : exit_code_(exit_code) {}

  int exit_code_;
};

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_SURFACE_ERROR_H_
