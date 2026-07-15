/// @file constraint_solver.cpp

#include "core/constraint_solver.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace coverwise {
namespace core {
namespace {

bool ConstraintsCanStillPass(const std::vector<model::Constraint>& constraints,
                             const std::vector<uint32_t>& assignment) {
  for (const auto& constraint : constraints) {
    if (constraint->Evaluate(assignment) == model::ConstraintResult::kFalse) {
      return false;
    }
  }
  return true;
}

bool Search(const std::vector<model::Parameter>& params,
            const std::vector<model::Constraint>& constraints, std::vector<uint32_t>& assignment,
            uint32_t assigned_count, const std::vector<std::vector<bool>>* allowed_values) {
  if (!ConstraintsCanStillPass(constraints, assignment)) {
    return false;
  }
  if (assigned_count == params.size()) {
    return true;
  }

  // Fail-first ordering: choose the unassigned parameter with the fewest valid
  // values. This materially bounds searches for contradictory constrained models.
  uint32_t next = model::kUnassigned;
  uint32_t fewest = UINT32_MAX;
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    if (assignment[pi] != model::kUnassigned) continue;
    uint32_t valid = 0;
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (allowed_values == nullptr ? !params[pi].is_invalid(vi) : (*allowed_values)[pi][vi]) {
        ++valid;
      }
    }
    if (valid < fewest) {
      fewest = valid;
      next = pi;
    }
  }
  if (next == model::kUnassigned || fewest == 0) {
    return false;
  }

  for (uint32_t vi = 0; vi < params[next].size(); ++vi) {
    if (allowed_values == nullptr ? params[next].is_invalid(vi) : !(*allowed_values)[next][vi]) {
      continue;
    }
    assignment[next] = vi;
    if (Search(params, constraints, assignment, assigned_count + 1, allowed_values)) {
      return true;
    }
  }
  assignment[next] = model::kUnassigned;
  return false;
}

}  // namespace

bool CompleteAssignment(const std::vector<model::Parameter>& params,
                        const std::vector<model::Constraint>& constraints,
                        const std::vector<std::vector<bool>>& allowed_values,
                        model::TestCase& assignment) {
  if (allowed_values.size() != params.size()) return false;
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    if (allowed_values[pi].size() != params[pi].size()) return false;
  }
  if (assignment.values.size() != params.size()) {
    assignment.values.resize(params.size(), model::kUnassigned);
  }

  uint32_t assigned_count = 0;
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    uint32_t vi = assignment.values[pi];
    if (vi == model::kUnassigned) continue;
    if (vi >= params[pi].size() || !allowed_values[pi][vi]) return false;
    ++assigned_count;
  }
  return Search(params, constraints, assignment.values, assigned_count, &allowed_values);
}

bool CompleteValidAssignment(const std::vector<model::Parameter>& params,
                             const std::vector<model::Constraint>& constraints,
                             model::TestCase& assignment) {
  if (assignment.values.size() != params.size()) {
    assignment.values.resize(params.size(), model::kUnassigned);
  }

  uint32_t assigned_count = 0;
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    uint32_t vi = assignment.values[pi];
    if (vi == model::kUnassigned) continue;
    if (vi >= params[pi].size() || params[pi].is_invalid(vi)) {
      return false;
    }
    ++assigned_count;
  }
  return Search(params, constraints, assignment.values, assigned_count, nullptr);
}

}  // namespace core
}  // namespace coverwise
