/// @file constraint_solver.h
/// @brief Constraint feasibility search used by the generation core.

#ifndef COVERWISE_CORE_CONSTRAINT_SOLVER_H_
#define COVERWISE_CORE_CONSTRAINT_SOLVER_H_

#include <vector>

#include "model/constraint_ast.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace coverwise {
namespace core {

/// Complete a partial assignment using the caller-provided allowed-value mask.
bool CompleteAssignment(const std::vector<model::Parameter>& params,
                        const std::vector<model::Constraint>& constraints,
                        const std::vector<std::vector<bool>>& allowed_values,
                        model::TestCase& assignment);

/// Complete a partial assignment using valid parameter values.
///
/// The search prunes an assignment as soon as any constraint is definitively
/// false. On success, @p assignment contains a complete satisfying witness.
/// Unassigned entries must use model::kUnassigned.
bool CompleteValidAssignment(const std::vector<model::Parameter>& params,
                             const std::vector<model::Constraint>& constraints,
                             model::TestCase& assignment);

}  // namespace core
}  // namespace coverwise

#endif  // COVERWISE_CORE_CONSTRAINT_SOLVER_H_
