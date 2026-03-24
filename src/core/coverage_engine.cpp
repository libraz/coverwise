/// @file coverage_engine.cpp

#include "core/coverage_engine.h"

#include <algorithm>
#include <vector>

#include "util/combinatorics.h"

namespace coverwise {
namespace core {

std::pair<CoverageEngine, model::Error> CoverageEngine::Create(
    const std::vector<model::Parameter>& params, uint32_t strength) {
  CoverageEngine engine;
  engine.params_ = params;
  engine.strength_ = strength;
  engine.InitCombinations();
  engine.total_tuples_ = engine.ComputeTotalTuples();
  engine.BuildLookupTables();

  if (engine.total_tuples_ > kMaxTuples) {
    model::Error err;
    err.code = model::Error::Code::kTupleExplosion;
    err.message = "t-wise tuple count exceeds safety limit";
    err.detail = "Total tuples: " + std::to_string(engine.total_tuples_) +
                 ", limit: " + std::to_string(kMaxTuples) + ". Reduce strength or parameter count.";
    return {CoverageEngine{}, err};
  }

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
  engine.InitCombinationsFromSubset();
  engine.total_tuples_ = engine.ComputeTotalTuples();
  engine.BuildLookupTables();

  if (engine.total_tuples_ > kMaxTuples) {
    model::Error err;
    err.code = model::Error::Code::kTupleExplosion;
    err.message = "t-wise tuple count exceeds safety limit";
    err.detail = "Total tuples: " + std::to_string(engine.total_tuples_) +
                 ", limit: " + std::to_string(kMaxTuples) + ". Reduce strength or parameter count.";
    return {CoverageEngine{}, err};
  }

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
  uint32_t total = 0;
  combination_offsets_.clear();
  combination_offsets_.reserve(param_combinations_.size());

  for (const auto& combo : param_combinations_) {
    combination_offsets_.push_back(total);
    uint32_t product = 1;
    for (uint32_t pi : combo) {
      product *= params_[pi].size();
    }
    total += product;
  }
  return total;
}

void CoverageEngine::AddTestCase(const model::TestCase& test_case) {
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
    const std::vector<model::Parameter>& params) const {
  std::vector<model::UncoveredTuple> result;

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];

    // Compute the number of value tuples for this combination.
    uint32_t product = 1;
    for (uint32_t pi : combo) {
      product *= params[pi].size();
    }

    // Enumerate all value tuples using mixed-radix decoding.
    for (uint32_t vi = 0; vi < product; ++vi) {
      uint32_t global_index = combination_offsets_[ci] + vi;
      if (covered_.Test(global_index)) continue;

      // Decode the mixed-radix index into value indices.
      model::UncoveredTuple ut;
      uint32_t remainder = vi;
      std::vector<uint32_t> value_indices(combo.size());
      for (int j = static_cast<int>(combo.size()) - 1; j >= 0; --j) {
        uint32_t radix = params[combo[j]].size();
        value_indices[j] = remainder % radix;
        remainder /= radix;
      }

      for (uint32_t j = 0; j < combo.size(); ++j) {
        uint32_t pi = combo[j];
        ut.params.push_back(params[pi].name);
        ut.tuple.push_back(params[pi].name + "=" + params[pi].values[value_indices[j]]);
      }

      result.push_back(std::move(ut));
    }
  }

  return result;
}

void CoverageEngine::ExcludeInvalidTuples(const std::vector<model::Constraint>& constraints) {
  if (constraints.empty()) return;

  uint32_t num_params = static_cast<uint32_t>(params_.size());

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];

    // Compute product for this combination.
    uint32_t product = 1;
    for (uint32_t pi : combo) {
      product *= params_[pi].size();
    }

    // Enumerate all value tuples.
    for (uint32_t vi = 0; vi < product; ++vi) {
      uint32_t global_index = combination_offsets_[ci] + vi;
      if (covered_.Test(global_index)) continue;  // Already marked.

      // Decode mixed-radix index into value indices.
      std::vector<uint32_t> value_indices(combo.size());
      uint32_t remainder = vi;
      for (int j = static_cast<int>(combo.size()) - 1; j >= 0; --j) {
        uint32_t radix = params_[combo[j]].size();
        value_indices[j] = remainder % radix;
        remainder /= radix;
      }

      // Build partial assignment with only this tuple's parameters set.
      std::vector<uint32_t> assignment(num_params, model::kUnassigned);
      for (uint32_t j = 0; j < combo.size(); ++j) {
        assignment[combo[j]] = value_indices[j];
      }

      // Evaluate all constraints against this partial assignment.
      bool invalid = false;
      for (const auto& constraint : constraints) {
        auto result = constraint->Evaluate(assignment);
        if (result == model::ConstraintResult::kFalse) {
          invalid = true;
          break;
        }
      }

      if (invalid) {
        covered_.Set(global_index);
        ++invalid_tuples_;
      }
    }
  }
}

void CoverageEngine::ExcludeInvalidValues() {
  // Check if any parameter has invalid values.
  bool has_invalid = false;
  for (const auto& p : params_) {
    if (p.has_invalid_values()) {
      has_invalid = true;
      break;
    }
  }
  if (!has_invalid) return;

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];

    // Compute product for this combination.
    uint32_t product = 1;
    for (uint32_t pi : combo) {
      product *= params_[pi].size();
    }

    // Enumerate all value tuples.
    for (uint32_t vi = 0; vi < product; ++vi) {
      uint32_t global_index = combination_offsets_[ci] + vi;
      if (covered_.Test(global_index)) continue;

      // Decode mixed-radix index into value indices.
      uint32_t remainder = vi;
      bool contains_invalid = false;
      for (int j = static_cast<int>(combo.size()) - 1; j >= 0; --j) {
        uint32_t pi = combo[j];
        uint32_t radix = params_[pi].size();
        uint32_t val_idx = remainder % radix;
        remainder /= radix;
        if (params_[pi].is_invalid(val_idx)) {
          contains_invalid = true;
          break;
        }
      }

      if (contains_invalid) {
        covered_.Set(global_index);
        ++invalid_tuples_;
      }
    }
  }
}

double CoverageEngine::CoverageRatio() const {
  if (TotalTuples() == 0) {
    return 1.0;
  }
  return static_cast<double>(CoveredCount()) / static_cast<double>(TotalTuples());
}

}  // namespace core
}  // namespace coverwise
