/// @file limits.h
/// @brief The documented input limits, defined once for every surface.
///
/// These decide what a model may declare, and every one of them is published in
/// the user-facing documentation, so they are part of what coverwise accepts
/// rather than of how it is tuned. The internal budgets that bound how much
/// work a decision may cost live in tuning_limits.h, which is not installed.

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
/// contract: the per-entity limits above decide what is accepted. It stops a
/// runaway or truncated stream from being read into memory without end.
///
/// It is applied to the bytes as they arrive, before any of them are parsed, so
/// it counts JSON syntax -- every brace, quote, colon and repeated key -- while
/// kMaxAggregateStringBytes counts only the text a caller supplied. For a wide
/// model the syntax dominates, and this bound is then the first one a document
/// meets: 100 parameters at kMaxTests rows is upwards of 100 MiB of JSON
/// carrying well under kMaxAggregateStringBytes of row text. For models of
/// ordinary width it sits far outside the limits above and a caller meets one
/// of those first.
///
/// A surface that reads documents must say in its own documentation that it
/// applies this bound, and must name the document rather than the row count
/// when it fires, since the suite may be well inside every limit above.
inline constexpr size_t kMaxDocumentBytes = 64 * 1024 * 1024;

}  // namespace model
}  // namespace coverwise

#endif  // COVERWISE_MODEL_LIMITS_H_
