/// @file constraint_solver.cpp

#include "core/constraint_solver.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <utility>
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
            const SolveParameterOrder& parameter_order, uint32_t order_position,
            const std::vector<std::vector<bool>>* allowed_values, SolveBudget& budget) {
  if (budget.remaining == 0) {
    budget.exceeded = true;
    return false;
  }
  --budget.remaining;
  if (!ConstraintsCanStillPass(constraints, assignment)) {
    return false;
  }
  while (order_position < parameter_order.size() &&
         assignment[parameter_order[order_position]] != model::kUnassigned) {
    ++order_position;
  }
  if (order_position == parameter_order.size()) return true;
  uint32_t next = parameter_order[order_position];

  for (uint32_t vi = 0; vi < params[next].size(); ++vi) {
    if (allowed_values == nullptr ? params[next].is_invalid(vi) : !(*allowed_values)[next][vi]) {
      continue;
    }
    assignment[next] = vi;
    if (Search(params, constraints, assignment, parameter_order, order_position + 1, allowed_values,
               budget)) {
      return true;
    }
    if (budget.exceeded) {
      assignment[next] = model::kUnassigned;
      return false;
    }
  }
  assignment[next] = model::kUnassigned;
  return false;
}

SolveParameterOrder BuildSolveParameterOrder(const std::vector<model::Parameter>& params,
                                             const std::vector<std::vector<bool>>* allowed_values) {
  std::vector<std::pair<uint32_t, uint32_t>> counts;
  counts.reserve(params.size());
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    uint32_t count = 0;
    for (uint32_t vi = 0; vi < params[pi].size(); ++vi) {
      if (allowed_values == nullptr ? !params[pi].is_invalid(vi) : (*allowed_values)[pi][vi]) {
        ++count;
      }
    }
    counts.emplace_back(count, pi);
  }
  std::stable_sort(counts.begin(), counts.end());
  SolveParameterOrder order;
  order.reserve(counts.size());
  for (const auto& [count, pi] : counts) {
    (void)count;
    order.push_back(pi);
  }
  return order;
}

}  // namespace

SolveParameterOrder BuildValidSolveParameterOrder(const std::vector<model::Parameter>& params) {
  return BuildSolveParameterOrder(params, nullptr);
}

SolveParameterOrder BuildAllowedSolveParameterOrder(
    const std::vector<model::Parameter>& params,
    const std::vector<std::vector<bool>>& allowed_values) {
  return BuildSolveParameterOrder(params, &allowed_values);
}

bool CompleteAssignment(const std::vector<model::Parameter>& params,
                        const std::vector<model::Constraint>& constraints,
                        const std::vector<std::vector<bool>>& allowed_values,
                        model::TestCase& assignment, SolveBudget* budget,
                        const SolveParameterOrder* parameter_order) {
  if (allowed_values.size() != params.size()) return false;
  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    if (allowed_values[pi].size() != params[pi].size()) return false;
  }
  if (assignment.values.size() != params.size()) {
    assignment.values.resize(params.size(), model::kUnassigned);
  }

  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    uint32_t vi = assignment.values[pi];
    if (vi == model::kUnassigned) continue;
    if (vi >= params[pi].size() || !allowed_values[pi][vi]) return false;
  }
  SolveBudget local;
  SolveBudget& b = budget ? *budget : local;
  SolveParameterOrder local_order;
  if (parameter_order == nullptr) {
    local_order = BuildAllowedSolveParameterOrder(params, allowed_values);
    parameter_order = &local_order;
  }
  return Search(params, constraints, assignment.values, *parameter_order, 0, &allowed_values, b);
}

bool CompleteValidAssignment(const std::vector<model::Parameter>& params,
                             const std::vector<model::Constraint>& constraints,
                             model::TestCase& assignment, SolveBudget* budget,
                             const SolveParameterOrder* parameter_order) {
  if (assignment.values.size() != params.size()) {
    assignment.values.resize(params.size(), model::kUnassigned);
  }

  for (uint32_t pi = 0; pi < params.size(); ++pi) {
    uint32_t vi = assignment.values[pi];
    if (vi == model::kUnassigned) continue;
    if (vi >= params[pi].size() || params[pi].is_invalid(vi)) {
      return false;
    }
  }
  SolveBudget local;
  SolveBudget& b = budget ? *budget : local;
  SolveParameterOrder local_order;
  if (parameter_order == nullptr) {
    local_order = BuildValidSolveParameterOrder(params);
    parameter_order = &local_order;
  }
  return Search(params, constraints, assignment.values, *parameter_order, 0, nullptr, b);
}

}  // namespace core
}  // namespace coverwise
