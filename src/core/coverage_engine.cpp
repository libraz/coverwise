/// @file coverage_engine.cpp

#include "core/coverage_engine.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace coverwise {
namespace core {

namespace {
constexpr uint32_t kUnassigned = std::numeric_limits<uint32_t>::max();
}  // namespace

std::pair<CoverageEngine, model::Error> CoverageEngine::Create(
    const std::vector<model::Parameter>& params, uint32_t strength) {
  CoverageEngine engine;
  engine.params_ = params;
  engine.strength_ = strength;
  engine.InitCombinations();
  engine.total_tuples_ = engine.ComputeTotalTuples();

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
  param_combinations_.clear();

  // No combinations possible if fewer parameters than strength.
  if (n < strength_ || strength_ == 0) {
    return;
  }

  // Generate all C(n, strength_) combinations using iterative approach.
  std::vector<uint32_t> indices(strength_);
  for (uint32_t i = 0; i < strength_; ++i) {
    indices[i] = i;
  }

  while (true) {
    param_combinations_.push_back(indices);

    // Find rightmost index that can be incremented.
    int pos = static_cast<int>(strength_) - 1;
    while (pos >= 0 && indices[pos] == n - strength_ + static_cast<uint32_t>(pos)) {
      --pos;
    }
    if (pos < 0) break;

    ++indices[pos];
    for (uint32_t j = static_cast<uint32_t>(pos) + 1; j < strength_; ++j) {
      indices[j] = indices[j - 1] + 1;
    }
  }
}

void CoverageEngine::InitCombinationsFromSubset() {
  uint32_t n = static_cast<uint32_t>(param_subset_.size());
  param_combinations_.clear();

  if (n < strength_ || strength_ == 0) {
    return;
  }

  // Generate all C(n, strength_) combinations of indices within the subset,
  // but store the GLOBAL param indices so AddTestCase/ScoreValue work directly.
  std::vector<uint32_t> local_indices(strength_);
  for (uint32_t i = 0; i < strength_; ++i) {
    local_indices[i] = i;
  }

  while (true) {
    // Map local indices to global param indices.
    std::vector<uint32_t> global_combo(strength_);
    for (uint32_t i = 0; i < strength_; ++i) {
      global_combo[i] = param_subset_[local_indices[i]];
    }
    param_combinations_.push_back(global_combo);

    // Find rightmost index that can be incremented.
    int pos = static_cast<int>(strength_) - 1;
    while (pos >= 0 && local_indices[pos] == n - strength_ + static_cast<uint32_t>(pos)) {
      --pos;
    }
    if (pos < 0) break;

    ++local_indices[pos];
    for (uint32_t j = static_cast<uint32_t>(pos) + 1; j < strength_; ++j) {
      local_indices[j] = local_indices[j - 1] + 1;
    }
  }
}

uint32_t CoverageEngine::ComputeTotalTuples() const {
  uint32_t total = 0;
  // const_cast needed because ComputeTotalTuples is declared const in the
  // header but must populate combination_offsets_. Called only during Create().
  auto& offsets = const_cast<std::vector<uint32_t>&>(combination_offsets_);
  offsets.clear();
  offsets.reserve(param_combinations_.size());

  for (const auto& combo : param_combinations_) {
    offsets.push_back(total);
    uint32_t product = 1;
    for (uint32_t pi : combo) {
      product *= params_[pi].size();
    }
    total += product;
  }
  return total;
}

uint32_t CoverageEngine::TupleIndex(const std::vector<uint32_t>& param_indices,
                                    const std::vector<uint32_t>& value_indices) const {
  // Find matching combination.
  for (uint32_t i = 0; i < param_combinations_.size(); ++i) {
    if (param_combinations_[i] == param_indices) {
      // Mixed-radix encoding within this combination's block.
      uint32_t local_index = 0;
      for (uint32_t j = 0; j < param_indices.size(); ++j) {
        local_index *= params_[param_indices[j]].size();
        local_index += value_indices[j];
      }
      return combination_offsets_[i] + local_index;
    }
  }
  // Should not reach here with valid input.
  return 0;
}

void CoverageEngine::AddTestCase(const model::TestCase& test_case) {
  std::vector<uint32_t> param_indices(strength_);
  std::vector<uint32_t> value_indices(strength_);

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];

    // Extract value indices for this combination.
    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index *= params_[combo[j]].size();
      local_index += test_case.values[combo[j]];
    }

    covered_.Set(combination_offsets_[ci] + local_index);
  }
}

uint32_t CoverageEngine::ScoreValue(const model::TestCase& partial, uint32_t param_index,
                                    uint32_t value_index) const {
  uint32_t score = 0;

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];

    // Check if this combination includes param_index.
    bool includes_param = false;
    for (uint32_t pi : combo) {
      if (pi == param_index) {
        includes_param = true;
        break;
      }
    }
    if (!includes_param) continue;

    // Check that all other parameters in the combination are assigned.
    bool all_assigned = true;
    for (uint32_t pi : combo) {
      if (pi != param_index && partial.values[pi] == kUnassigned) {
        all_assigned = false;
        break;
      }
    }
    if (!all_assigned) continue;

    // Compute tuple index using mixed-radix encoding.
    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index *= params_[combo[j]].size();
      if (combo[j] == param_index) {
        local_index += value_index;
      } else {
        local_index += partial.values[combo[j]];
      }
    }

    uint32_t global_index = combination_offsets_[ci] + local_index;
    if (!covered_.Test(global_index)) {
      ++score;
    }
  }

  return score;
}

uint32_t CoverageEngine::ScoreCandidate(const model::TestCase& candidate) const {
  uint32_t score = 0;

  for (uint32_t ci = 0; ci < param_combinations_.size(); ++ci) {
    const auto& combo = param_combinations_[ci];

    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index *= params_[combo[j]].size();
      local_index += candidate.values[combo[j]];
    }

    uint32_t global_index = combination_offsets_[ci] + local_index;
    if (!covered_.Test(global_index)) {
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
      std::vector<uint32_t> assignment(num_params, kUnassigned);
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
