/// @file coverage_engine.cpp

#include "core/coverage_engine.h"

#include <algorithm>
#include <cassert>
#include <limits>
#include <vector>

#include "core/constraint_solver.h"
#include "model/parameter.h"
#include "util/combinatorics.h"

namespace coverwise {
namespace core {

namespace {

/// @brief Build an error for when tuple count exceeds the safety limit.
model::Error MakeTupleExplosionError(uint64_t total_tuples, uint64_t max_tuples) {
  model::Error err;
  err.code = model::Error::Code::kTupleExplosion;
  err.message = "t-wise tuple count exceeds safety limit";
  err.detail = "Total tuples: " + std::to_string(total_tuples) +
               ", limit: " + std::to_string(max_tuples) + ". Reduce strength or parameter count.";
  return err;
}

model::Error PreflightModel(const std::vector<model::Parameter>& params,
                            const std::vector<uint32_t>& subset, uint32_t strength,
                            uint32_t& total_tuples) {
  const uint32_t n = static_cast<uint32_t>(subset.empty() ? params.size() : subset.size());
  if (strength == 0 || strength > n) {
    total_tuples = 0;
    return {};
  }

  uint64_t combination_count = 0;
  if (!util::CheckedBinomial(n, strength, CoverageEngine::kMaxCombinations, combination_count)) {
    model::Error err;
    err.code = model::Error::Code::kTupleExplosion;
    err.message = "parameter combination metadata exceeds safety limit";
    err.detail = "Combinations exceed limit: " + std::to_string(CoverageEngine::kMaxCombinations) +
                 ". Reduce strength or parameter count.";
    return err;
  }

  std::vector<uint32_t> combo(strength);
  for (uint32_t i = 0; i < strength; ++i) combo[i] = i;
  uint64_t total = 0;
  for (;;) {
    uint64_t product = 1;
    for (uint32_t local : combo) {
      uint32_t pi = subset.empty() ? local : subset[local];
      uint64_t radix = params[pi].size();
      if (radix != 0 && product > CoverageEngine::kMaxTuples / radix) {
        return MakeTupleExplosionError(CoverageEngine::kMaxTuples + 1ULL,
                                       CoverageEngine::kMaxTuples);
      }
      product *= radix;
    }
    if (total > CoverageEngine::kMaxTuples - product) {
      return MakeTupleExplosionError(total + product, CoverageEngine::kMaxTuples);
    }
    total += product;

    int pos = static_cast<int>(strength) - 1;
    while (pos >= 0 && combo[pos] == n - strength + static_cast<uint32_t>(pos)) --pos;
    if (pos < 0) break;
    ++combo[pos];
    for (uint32_t i = static_cast<uint32_t>(pos) + 1; i < strength; ++i) {
      combo[i] = combo[i - 1] + 1;
    }
  }
  total_tuples = static_cast<uint32_t>(total);
  return {};
}

}  // namespace

std::pair<CoverageEngine, model::Error> CoverageEngine::Create(
    const std::vector<model::Parameter>& params, uint32_t strength) {
  CoverageEngine engine;
  engine.params_ = params;
  engine.strength_ = strength;
  auto preflight_error = PreflightModel(params, {}, strength, engine.total_tuples_);
  if (!preflight_error.ok()) return {CoverageEngine{}, preflight_error};
  engine.InitCombinations();
  engine.total_tuples_ = engine.ComputeTotalTuples();

  if (engine.total_tuples_ > kMaxTuples) {
    return {CoverageEngine{}, MakeTupleExplosionError(engine.total_tuples_, kMaxTuples)};
  }

  engine.BuildLookupTables();
  engine.covered_ = util::DynamicBitset(engine.total_tuples_);
  return {std::move(engine), model::Error{}};
}

std::pair<CoverageEngine, model::Error> CoverageEngine::Create(
    const std::vector<model::Parameter>& all_params, const std::vector<uint32_t>& param_subset,
    uint32_t strength) {
  CoverageEngine engine;
  engine.params_ = all_params;
  engine.strength_ = strength;
  engine.param_subset_ = param_subset;
  auto preflight_error = PreflightModel(all_params, param_subset, strength, engine.total_tuples_);
  if (!preflight_error.ok()) return {CoverageEngine{}, preflight_error};
  engine.InitCombinationsFromSubset();
  engine.total_tuples_ = engine.ComputeTotalTuples();

  if (engine.total_tuples_ > kMaxTuples) {
    return {CoverageEngine{}, MakeTupleExplosionError(engine.total_tuples_, kMaxTuples)};
  }

  engine.BuildLookupTables();
  engine.covered_ = util::DynamicBitset(engine.total_tuples_);
  return {std::move(engine), model::Error{}};
}

void CoverageEngine::InitCombinations() {
  uint32_t n = static_cast<uint32_t>(params_.size());
  param_combinations_ = util::GenerateCombinations(n, strength_);
}

void CoverageEngine::InitCombinationsFromSubset() {
  uint32_t n = static_cast<uint32_t>(param_subset_.size());
  auto local_combos = util::GenerateCombinations(n, strength_);

  // Map local indices to global param indices.
  param_combinations_.clear();
  param_combinations_.reserve(local_combos.size());
  for (const auto& local : local_combos) {
    std::vector<uint32_t> global_combo(strength_);
    for (uint32_t i = 0; i < strength_; ++i) {
      global_combo[i] = param_subset_[local[i]];
    }
    param_combinations_.push_back(std::move(global_combo));
  }
}

void CoverageEngine::BuildLookupTables() {
  uint32_t num_params = static_cast<uint32_t>(params_.size());
  uint32_t num_combos = static_cast<uint32_t>(param_combinations_.size());

  // Build param-to-combinations index and position-in-combo lookup.
  param_to_combos_.assign(num_params, {});
  param_position_in_combo_.assign(num_params, {});

  for (uint32_t ci = 0; ci < num_combos; ++ci) {
    const auto& combo = param_combinations_[ci];
    for (uint32_t j = 0; j < strength_; ++j) {
      uint32_t pi = combo[j];
      param_to_combos_[pi].push_back(ci);
      param_position_in_combo_[pi].push_back(j);
    }
  }

  // Build mixed-radix multipliers for each combination.
  // combo_multipliers_[ci][j] = product of value counts for positions j+1..t-1.
  combo_multipliers_.resize(num_combos);
  for (uint32_t ci = 0; ci < num_combos; ++ci) {
    const auto& combo = param_combinations_[ci];
    auto& mults = combo_multipliers_[ci];
    mults.resize(strength_);
    mults[strength_ - 1] = 1;
    for (int j = static_cast<int>(strength_) - 2; j >= 0; --j) {
      mults[j] = mults[j + 1] * params_[combo[j + 1]].size();
    }
  }
}

uint32_t CoverageEngine::ComputeTotalTuples() {
  uint64_t total = 0;
  combination_offsets_.clear();
  combination_offsets_.reserve(param_combinations_.size());

  for (const auto& combo : param_combinations_) {
    combination_offsets_.push_back(static_cast<uint32_t>(total));
    uint64_t product = 1;
    for (uint32_t pi : combo) {
      product *= params_[pi].size();
      if (product > kMaxTuples) {
        return static_cast<uint32_t>(std::min(total + product, static_cast<uint64_t>(UINT32_MAX)));
      }
    }
    total += product;
    if (total > kMaxTuples) {
      return static_cast<uint32_t>(std::min(total, static_cast<uint64_t>(UINT32_MAX)));
    }
  }
  return static_cast<uint32_t>(total);
}

void CoverageEngine::AddTestCase(const model::TestCase& test_case) {
  assert(test_case.values.size() >= params_.size());
  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];
    const auto& mults = combo_multipliers_[ci];

    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index += test_case.values[combo[j]] * mults[j];
    }

    covered_.Set(combination_offsets_[ci] + local_index);
  }
}

uint32_t CoverageEngine::ScoreValue(const model::TestCase& partial, uint32_t param_index,
                                    uint32_t value_index) const {
  assert(partial.values.size() >= params_.size());
  uint32_t score = 0;
  const auto& relevant_combos = param_to_combos_[param_index];
  const auto& positions = param_position_in_combo_[param_index];
  uint32_t num_relevant = static_cast<uint32_t>(relevant_combos.size());

  for (uint32_t k = 0; k < num_relevant; ++k) {
    uint32_t ci = relevant_combos[k];
    uint32_t pos = positions[k];
    const auto& combo = param_combinations_[ci];
    const auto& mults = combo_multipliers_[ci];

    // Check all other params are assigned and compute mixed-radix index.
    bool all_assigned = true;
    uint32_t local_index = value_index * mults[pos];
    for (uint32_t j = 0; j < strength_; ++j) {
      if (j == pos) continue;
      uint32_t v = partial.values[combo[j]];
      if (v == model::kUnassigned) {
        all_assigned = false;
        break;
      }
      local_index += v * mults[j];
    }
    if (!all_assigned) continue;

    if (!covered_.Test(combination_offsets_[ci] + local_index)) {
      ++score;
    }
  }

  return score;
}

uint32_t CoverageEngine::ScoreCandidate(const model::TestCase& candidate) const {
  assert(candidate.values.size() >= params_.size());
  uint32_t score = 0;

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];
    const auto& mults = combo_multipliers_[ci];

    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index += candidate.values[combo[j]] * mults[j];
    }

    if (!covered_.Test(combination_offsets_[ci] + local_index)) {
      ++score;
    }
  }

  return score;
}

std::vector<model::UncoveredTuple> CoverageEngine::GetUncoveredTuples(
    const std::vector<model::Parameter>& params, uint32_t limit) const {
  std::vector<model::UncoveredTuple> result;
  result.reserve(std::min(limit, TotalTuples() - CoveredCount()));

  ForEachTupleUntil([&](uint32_t /*global_index*/, const std::vector<uint32_t>& combo,
                        const std::vector<uint32_t>& value_indices) {
    if (result.size() >= limit) return false;
    model::UncoveredTuple ut;
    for (uint32_t j = 0; j < combo.size(); ++j) {
      uint32_t pi = combo[j];
      ut.params.push_back(params[pi].name);
      ut.tuple.push_back(params[pi].name + "=" + params[pi].values[value_indices[j]]);
    }
    result.push_back(std::move(ut));
    return result.size() < limit;
  });

  return result;
}

void CoverageEngine::ExcludeInvalidTuples(const std::vector<model::Constraint>& constraints,
                                          const std::vector<std::vector<bool>>& allowed_values) {
  if (constraints.empty()) return;

  uint32_t num_params = static_cast<uint32_t>(params_.size());
  std::vector<uint32_t> assignment(num_params, model::kUnassigned);

  ForEachTuple([&](uint32_t global_index, const std::vector<uint32_t>& combo,
                   const std::vector<uint32_t>& value_indices) {
    // Build partial assignment with only this tuple's parameters set.
    for (uint32_t j = 0; j < combo.size(); ++j) {
      assignment[combo[j]] = value_indices[j];
    }

    // A tuple belongs to the coverage universe only when it can be extended to
    // a complete assignment using valid values. Evaluating the partial tuple
    // alone is insufficient for interacting implications.
    model::TestCase witness{assignment};
    bool invalid = allowed_values.empty()
                       ? !CompleteValidAssignment(params_, constraints, witness)
                       : !CompleteAssignment(params_, constraints, allowed_values, witness);

    if (invalid && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
    }

    // Reset assignment for reuse.
    for (uint32_t j = 0; j < combo.size(); ++j) {
      assignment[combo[j]] = model::kUnassigned;
    }
  });
}

void CoverageEngine::ExcludeInvalidValues() {
  if (!model::HasInvalidValues(params_)) return;

  ForEachTuple([&](uint32_t global_index, const std::vector<uint32_t>& combo,
                   const std::vector<uint32_t>& value_indices) {
    // Check if any decoded value is invalid.
    bool contains_invalid = false;
    for (size_t j = 0; j < combo.size(); ++j) {
      if (params_[combo[j]].is_invalid(value_indices[j])) {
        contains_invalid = true;
        break;
      }
    }

    if (contains_invalid && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
    }
  });
}

void CoverageEngine::ExcludeTuplesOutsideMask(
    const std::vector<std::vector<bool>>& allowed_values) {
  if (allowed_values.size() != params_.size()) return;
  ForEachTuple([&](uint32_t global_index, const std::vector<uint32_t>& combo,
                   const std::vector<uint32_t>& value_indices) {
    bool excluded = false;
    for (size_t j = 0; j < combo.size(); ++j) {
      uint32_t pi = combo[j];
      uint32_t vi = value_indices[j];
      if (allowed_values[pi].size() != params_[pi].size() || !allowed_values[pi][vi]) {
        excluded = true;
        break;
      }
    }
    if (excluded && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
    }
  });
}

void CoverageEngine::ExcludeTuplesNotContaining(uint32_t param_index, uint32_t value_index) {
  ForEachTuple([&](uint32_t global_index, const std::vector<uint32_t>& combo,
                   const std::vector<uint32_t>& value_indices) {
    bool contains = false;
    for (size_t j = 0; j < combo.size(); ++j) {
      if (combo[j] == param_index && value_indices[j] == value_index) {
        contains = true;
        break;
      }
    }
    if (!contains && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
    }
  });
}

double CoverageEngine::CoverageRatio() const {
  if (TotalTuples() == 0) {
    return 1.0;
  }
  return static_cast<double>(CoveredCount()) / static_cast<double>(TotalTuples());
}

}  // namespace core
}  // namespace coverwise
