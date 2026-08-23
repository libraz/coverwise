/// @file limits.h
/// @brief The documented input limits, defined once for every surface.

#ifndef COVERWISE_MODEL_LIMITS_H_
#define COVERWISE_MODEL_LIMITS_H_

#include <cstddef>

namespace coverwise {
namespace model {

/// @brief Maximum number of parameters a single model may declare.
///
/// The limit is what keeps feasibility search bounded — the search walks one
/// parameter per level, and a satisfying chain spends only one node of the
/// search budget per level, so parameter count is the only thing bounding how
/// deep a search can go.
inline constexpr size_t kMaxParameters = 1024;

/// @brief Maximum number of values a single parameter may declare.
inline constexpr size_t kMaxValuesPerParameter = 16384;

/// @brief Maximum number of rows in a `tests`, `seeds`, or `existing` array.
inline constexpr size_t kMaxTests = 100000;

/// @brief Maximum number of constraint expressions in a single model.
inline constexpr size_t kMaxConstraints = 256;

/// @brief Maximum UTF-8 byte length of any single input string.
inline constexpr size_t kMaxStringBytes = 64 * 1024;

/// @brief Maximum total UTF-8 byte length of the strings in one input.
inline constexpr size_t kMaxAggregateStringBytes = 1024 * 1024;

/// @brief Upper bound on the raw bytes of one JSON document a surface reads.
///
/// This is a memory guard for file/stdin reads, not part of the acceptance
/// contract: the per-entity limits above decide what is accepted. It is sized
/// well above what a document satisfying those limits needs (a kMaxTests-row
/// suite over a realistic parameter count is a few tens of megabytes of JSON
/// syntax, while the strings it carries stay inside kMaxAggregateStringBytes),
/// so a caller never meets this bound before meeting a documented one. A
/// surface that reads documents must say in its own documentation that it
/// applies this bound.
inline constexpr size_t kMaxDocumentBytes = 64 * 1024 * 1024;

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_LIMITS_H_
