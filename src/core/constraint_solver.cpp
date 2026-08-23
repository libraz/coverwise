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

/// @brief Smallest usable value index at or after @p start, or the domain size.
uint32_t NextUsableValue(const std::vector<model::Parameter>& params,
                         const std::vector<std::vector<bool>>* allowed_values, uint32_t param,
                         uint32_t start) {
  for (uint32_t vi = start; vi < params[param].size(); ++vi) {
    if (allowed_values == nullptr ? !params[param].is_invalid(vi) : (*allowed_values)[param][vi]) {
      return vi;
    }
  }
  return static_cast<uint32_t>(params[param].size());
}

/// @brief Depth-first feasibility search driven by an explicit stack.
///
/// The search depth grows with the parameter count, and a satisfiable chain
/// spends only one node of the budget per level, so the node budget alone does
/// not bound how deep the search goes. Keeping the frames on the heap makes
/// stack use independent of the model size while preserving the recursive
/// enumeration order, budget accounting and assignment side effects: on success
/// @p assignment holds the witness, and on failure every parameter this search
/// assigned is restored to model::kUnassigned.
bool Search(const std::vector<model::Parameter>& params,
            const std::vector<model::Constraint>& constraints, std::vector<uint32_t>& assignment,
            const SolveParameterOrder& parameter_order, uint32_t order_position,
            const std::vector<std::vector<bool>>* allowed_values, SolveBudget& budget) {
  struct Frame {
    uint32_t param;          ///< Parameter assigned at this level.
    uint32_t value;          ///< Value currently being tried.
    uint32_t next_position;  ///< Order position the level below starts from.
  };
  std::vector<Frame> stack;
  uint32_t position = order_position;
  bool expand = true;

  for (;;) {
    if (expand) {
      expand = false;
      bool dead = false;
      if (budget.remaining == 0) {
        budget.exceeded = true;
        dead = true;
      } else {
        --budget.remaining;
        dead = !ConstraintsCanStillPass(constraints, assignment);
      }
      if (!dead) {
        while (position < parameter_order.size() &&
               assignment[parameter_order[position]] != model::kUnassigned) {
          ++position;
        }
        if (position == parameter_order.size()) return true;
        uint32_t next = parameter_order[position];
        uint32_t vi = NextUsableValue(params, allowed_values, next, 0);
        if (vi < params[next].size()) {
          assignment[next] = vi;
          ++position;
          stack.push_back({next, vi, position});
          expand = true;
          continue;
        }
      }
    }

    // Backtrack. An exhausted budget unwinds without trying further values, so
    // the caller sees an untouched assignment together with budget.exceeded.
    if (stack.empty()) return false;
    Frame& top = stack.back();
    uint32_t vi = budget.exceeded
                      ? static_cast<uint32_t>(params[top.param].size())
                      : NextUsableValue(params, allowed_values, top.param, top.value + 1);
    if (vi < params[top.param].size()) {
      top.value = vi;
      assignment[top.param] = vi;
      position = top.next_position;
      expand = true;
      continue;
    }
    assignment[top.param] = model::kUnassigned;
    stack.pop_back();
  }
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
