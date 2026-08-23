/// @file coverage_validator.h
/// @brief Independent coverage validation (does NOT depend on generator/core).

#ifndef COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_
#define COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_

#include <cstdint>
#include <string>
#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace validator {

/// @brief Coverage validation report with human-readable uncovered tuples.
struct CoverageReport {
  uint64_t total_tuples = 0;
  uint64_t covered_tuples = 0;
  double coverage_ratio = 0.0;
  /// @brief Human-readable uncovered tuples (e.g. "os=win, browser=safari").
  std::vector<model::UncoveredTuple> uncovered;
  uint64_t uncovered_count = 0;
  uint64_t omitted_uncovered = 0;
  struct InvalidTest {
    uint32_t test_index = 0;
    std::string reason;
  };
  std::vector<InvalidTest> invalid_tests;
  model::Error error;
};

/// @brief Equivalence class coverage report.
struct ClassCoverageReport {
  uint64_t total_class_tuples = 0;
  uint64_t covered_class_tuples = 0;
  double coverage_ratio = 0.0;
  model::Error error;
};

/// @brief Independently validate t-wise coverage of a test suite.
///
/// This validator enumerates all t-tuples from scratch (not using any
/// generator internals) and checks each against the test suite. Tuples
/// containing a value marked invalid are excluded from the coverage universe
/// (they do not count toward total_tuples or uncovered), matching the
/// generator's CoverageEngine::ExcludeInvalidValues semantics.
///
/// @param constraints Optional constraint AST list. A tuple is excluded from the
///                    coverage universe entirely (it does not count toward
///                    total_tuples or uncovered) when it has no constraint-
///                    satisfying completion — i.e. its partial assignment cannot
///                    be extended to any full assignment of valid values that
///                    satisfies every constraint. This is a completion-witness
///                    test, stronger than checking whether the partial tuple
///                    alone evaluates to kFalse (interacting implications can
///                    make an individually-consistent tuple unreachable). It
///                    matches the generator's ExcludeInvalidTuples semantics.
CoverageReport ValidateCoverage(const std::vector<model::Parameter>& params,
                                const std::vector<model::TestCase>& tests, uint32_t strength,
                                const std::vector<model::Constraint>& constraints = {});

/// @brief Compute equivalence class coverage for a test suite.
///
/// Maps each value to its equivalence class and enumerates all t-wise class
/// tuples, counting how many are covered by the test suite.
/// Only considers parameters that have equivalence classes defined.
///
/// A class tuple is included in the universe only if it has at least one
/// representative value tuple that contains no invalid value and satisfies all
/// constraints. Class tuples whose every representative is excluded (by an
/// invalid value or a violated constraint) are dropped from the universe so a
/// fully valid-covering suite is not penalized with classCoverageRatio < 1.0.
/// @param constraints Optional constraint AST list used to decide whether a
///                    class tuple has a constraint-satisfiable representative.
/// @return Class coverage report. An empty class universe — strength outside
///         [1, parameter count], no parameter carrying equivalence classes, or
///         every class tuple excluded as infeasible — reports zero counts with
///         coverage_ratio 1.0 and an ok error, so a suite is never penalized for
///         a universe with nothing to cover. coverage_ratio is left at 0.0 only
///         on an error exit (invalid parameters, unsatisfiable constraints, an
///         exceeded enumeration limit, or an exhausted feasibility budget),
///         which is signalled by a non-ok error and where the counts are
///         partial. Detect "no classes" via the counts and error, never via
///         coverage_ratio == 0.0.
ClassCoverageReport ComputeClassCoverage(const std::vector<model::Parameter>& params,
                                         const std::vector<model::TestCase>& tests,
                                         uint32_t strength,
                                         const std::vector<model::Constraint>& constraints = {});

/// @brief Annotate a GenerateResult with equivalence class coverage if applicable.
///
/// Checks whether any parameter has equivalence classes defined. If so,
/// computes class coverage and sets the class_coverage optional
/// fields on the result.
/// @param result The generate result to annotate (modified in place).
/// @param params The parameter definitions (with equivalence classes).
/// @param strength The coverage strength used for generation.
/// @param constraints Optional constraints threaded into class-tuple enumeration.
void AnnotateClassCoverage(model::GenerateResult& result,
                           const std::vector<model::Parameter>& params, uint32_t strength,
                           const std::vector<model::Constraint>& constraints = {});

}  // namespace validator
}  // namespace coverwise

#endif  // COVERWISE_VALIDATOR_COVERAGE_VALIDATOR_H_
