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
  if (!util::CheckedBinomial(n, strength, util::BinomialLimit(model::kMaxCombinations),
                             combination_count)) {
    model::Error err;
    err.code = model::Error::Code::kTupleExplosion;
    err.message = "parameter combination metadata exceeds safety limit";
    err.detail = "Combinations exceed limit: " + std::to_string(model::kMaxCombinations) +
                 ". Reduce strength or parameter count.";
    return err;
  }

  std::vector<uint32_t> combo(strength);
  for (uint32_t i = 0; i < strength; ++i) combo[i] = i;
  uint64_t total = 0;
  constexpr uint64_t kU64Max = std::numeric_limits<uint64_t>::max();
  for (;;) {
    // Compute the full product for this combination, saturating at UINT64_MAX so
    // the reported figure reflects the real (approximate) magnitude rather than a
    // fixed sentinel just past the limit.
    uint64_t product = 1;
    for (uint32_t local : combo) {
      uint32_t pi = subset.empty() ? local : subset[local];
      uint64_t radix = params[pi].size();
      product = (radix != 0 && product > kU64Max / radix) ? kU64Max : product * radix;
    }
    if (product > model::kMaxTuples) {
      return MakeTupleExplosionError(product, model::kMaxTuples);
    }
    if (total > model::kMaxTuples - product) {
      uint64_t reported = (total > kU64Max - product) ? kU64Max : total + product;
      return MakeTupleExplosionError(reported, model::kMaxTuples);
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

std::pair<CoverageEngine, model::Error> CoverageEngine::CreateShared(SharedParameters params,
                                                                     uint32_t strength) {
  CoverageEngine engine;
  engine.params_ = std::move(params);
  engine.strength_ = strength;
  auto preflight_error = PreflightModel(engine.Parameters(), {}, strength, engine.total_tuples_);
  if (!preflight_error.ok()) return {CoverageEngine{}, preflight_error};
  engine.InitCombinations();
  engine.total_tuples_ = engine.ComputeTotalTuples();

  if (engine.total_tuples_ > model::kMaxTuples) {
    return {CoverageEngine{}, MakeTupleExplosionError(engine.total_tuples_, model::kMaxTuples)};
  }

  engine.BuildLookupTables();
  engine.covered_ = util::DynamicBitset(engine.total_tuples_);
  return {std::move(engine), model::Error{}};
}

std::pair<CoverageEngine, model::Error> CoverageEngine::CreateShared(
    SharedParameters all_params, const std::vector<uint32_t>& param_subset, uint32_t strength) {
  CoverageEngine engine;
  engine.params_ = std::move(all_params);
  engine.strength_ = strength;
  engine.param_subset_ = param_subset;
  auto preflight_error =
      PreflightModel(engine.Parameters(), param_subset, strength, engine.total_tuples_);
  if (!preflight_error.ok()) return {CoverageEngine{}, preflight_error};
  engine.InitCombinationsFromSubset();
  engine.total_tuples_ = engine.ComputeTotalTuples();

  if (engine.total_tuples_ > model::kMaxTuples) {
    return {CoverageEngine{}, MakeTupleExplosionError(engine.total_tuples_, model::kMaxTuples)};
  }

  engine.BuildLookupTables();
  engine.covered_ = util::DynamicBitset(engine.total_tuples_);
  return {std::move(engine), model::Error{}};
}

void CoverageEngine::InitCombinations() {
  // Strength 0 selects no parameters at all: there is no combination to store
  // and no stride to divide by. PreflightModel already accepts it as an empty
  // tuple universe, so the engine is built empty rather than rejected.
  if (strength_ == 0) {
    param_combinations_.clear();
    num_combinations_ = 0;
    return;
  }
  uint32_t n = static_cast<uint32_t>(Parameters().size());
  param_combinations_ = util::GenerateCombinationsFlat(n, strength_);
  num_combinations_ = static_cast<uint32_t>(param_combinations_.size() / strength_);
}

void CoverageEngine::InitCombinationsFromSubset() {
  if (strength_ == 0) {
    param_combinations_.clear();
    num_combinations_ = 0;
    return;
  }
  uint32_t n = static_cast<uint32_t>(param_subset_.size());
  param_combinations_ = util::GenerateCombinationsFlat(n, strength_);
  for (uint32_t& local_index : param_combinations_) local_index = param_subset_[local_index];
  num_combinations_ = static_cast<uint32_t>(param_combinations_.size() / strength_);
}

void CoverageEngine::BuildLookupTables() {
  uint32_t num_params = static_cast<uint32_t>(Parameters().size());
  uint32_t num_combos = num_combinations_;

  // Build param-to-combinations index and position-in-combo lookup.
  param_to_combos_.assign(num_params, {});
  param_position_in_combo_.assign(num_params, {});

  for (uint32_t ci = 0; ci < num_combos; ++ci) {
    const uint32_t* combo = Combo(ci);
    for (uint32_t j = 0; j < strength_; ++j) {
      uint32_t pi = combo[j];
      param_to_combos_[pi].push_back(ci);
      param_position_in_combo_[pi].push_back(j);
    }
  }

  // Build mixed-radix multipliers for each combination, stored flat.
  // Mults(ci)[j] = product of value counts for positions j+1..t-1.
  combo_multipliers_.resize(static_cast<size_t>(num_combos) * strength_);
  for (uint32_t ci = 0; ci < num_combos; ++ci) {
    const uint32_t* combo = Combo(ci);
    uint32_t* mults = &combo_multipliers_[static_cast<size_t>(ci) * strength_];
    mults[strength_ - 1] = 1;
    for (int j = static_cast<int>(strength_) - 2; j >= 0; --j) {
      mults[j] = mults[j + 1] * Parameters()[combo[j + 1]].size();
    }
  }
}

uint32_t CoverageEngine::ComputeTotalTuples() {
  uint64_t total = 0;
  combination_offsets_.clear();
  combination_offsets_.reserve(num_combinations_);

  for (uint32_t ci = 0; ci < num_combinations_; ++ci) {
    const uint32_t* combo = Combo(ci);
    combination_offsets_.push_back(static_cast<uint32_t>(total));
    uint64_t product = 1;
    for (uint32_t j = 0; j < strength_; ++j) {
      product *= Parameters()[combo[j]].size();
      if (product > model::kMaxTuples) {
        return static_cast<uint32_t>(std::min(total + product, static_cast<uint64_t>(UINT32_MAX)));
      }
    }
    total += product;
    if (total > model::kMaxTuples) {
      return static_cast<uint32_t>(std::min(total, static_cast<uint64_t>(UINT32_MAX)));
    }
  }
  return static_cast<uint32_t>(total);
}

void CoverageEngine::AddTestCase(const model::TestCase& test_case) {
  assert(test_case.values.size() >= Parameters().size());
  for (uint32_t ci = 0; ci < num_combinations_; ++ci) {
    const uint32_t* combo = Combo(ci);
    const uint32_t* mults = Mults(ci);

    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index += test_case.values[combo[j]] * mults[j];
    }

    const uint32_t index = combination_offsets_[ci] + local_index;
    if (!covered_.Test(index)) {
      covered_.Set(index);
      ++covered_bits_;
    }
  }
}

uint32_t CoverageEngine::ScoreValue(const model::TestCase& partial, uint32_t param_index,
                                    uint32_t value_index) const {
  assert(partial.values.size() >= Parameters().size());
  uint32_t score = 0;
  const auto& relevant_combos = param_to_combos_[param_index];
  const auto& positions = param_position_in_combo_[param_index];
  uint32_t num_relevant = static_cast<uint32_t>(relevant_combos.size());

  for (uint32_t k = 0; k < num_relevant; ++k) {
    uint32_t ci = relevant_combos[k];
    uint32_t pos = positions[k];
    const uint32_t* combo = Combo(ci);
    const uint32_t* mults = Mults(ci);

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

void CoverageEngine::AddValueScores(const model::TestCase& partial, uint32_t param_index,
                                    uint32_t* out_scores) const {
  assert(partial.values.size() >= Parameters().size());
  const auto& relevant_combos = param_to_combos_[param_index];
  const auto& positions = param_position_in_combo_[param_index];
  const uint32_t num_relevant = static_cast<uint32_t>(relevant_combos.size());
  const uint32_t num_values = Parameters()[param_index].size();
  value_score_combo_visits_ += num_relevant;

  for (uint32_t k = 0; k < num_relevant; ++k) {
    uint32_t ci = relevant_combos[k];
    uint32_t pos = positions[k];
    const uint32_t* combo = Combo(ci);
    const uint32_t* mults = Mults(ci);

    // The other positions of this combination are the same for every candidate
    // value, so their share of the mixed-radix index is computed once here
    // rather than once per value.
    bool all_assigned = true;
    uint32_t base_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      if (j == pos) continue;
      uint32_t v = partial.values[combo[j]];
      if (v == model::kUnassigned) {
        all_assigned = false;
        break;
      }
      base_index += v * mults[j];
    }
    if (!all_assigned) continue;

    const uint32_t first = combination_offsets_[ci] + base_index;
    const uint32_t stride = mults[pos];
    for (uint32_t vi = 0; vi < num_values; ++vi) {
      if (!covered_.Test(first + vi * stride)) {
        ++out_scores[vi];
      }
    }
  }
}

uint32_t CoverageEngine::ScoreCandidate(const model::TestCase& candidate) const {
  assert(candidate.values.size() >= Parameters().size());
  uint32_t score = 0;

  for (uint32_t ci = 0; ci < num_combinations_; ++ci) {
    const uint32_t* combo = Combo(ci);
    const uint32_t* mults = Mults(ci);

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

  ForEachTupleUntil([&](uint32_t /*global_index*/, const uint32_t* combo,
                        const std::vector<uint32_t>& value_indices) {
    if (result.size() >= limit) return false;
    model::UncoveredTuple ut;
    for (uint32_t j = 0; j < strength_; ++j) {
      uint32_t pi = combo[j];
      ut.params.push_back(params[pi].name);
      ut.tuple.push_back(params[pi].name + "=" + params[pi].values[value_indices[j]]);
      ut.indices.emplace_back(pi, value_indices[j]);
    }
    result.push_back(std::move(ut));
    return result.size() < limit;
  });

  return result;
}

bool CoverageEngine::NeedsTuple(const uint32_t* params, const uint32_t* value_indices,
                                uint32_t count) const {
  if (count == 0 || count != strength_) return false;
  if (params[0] >= param_to_combos_.size()) return false;

  // Combinations are stored with their parameter indices ascending, so the
  // caller's ascending tuple either matches one of the combinations containing
  // its first parameter or is not enumerated here at all.
  for (uint32_t ci : param_to_combos_[params[0]]) {
    const uint32_t* combo = Combo(ci);
    bool same = true;
    for (uint32_t j = 0; j < strength_; ++j) {
      if (combo[j] != params[j]) {
        same = false;
        break;
      }
    }
    if (!same) continue;
    const uint32_t* mults = Mults(ci);
    uint32_t local_index = 0;
    for (uint32_t j = 0; j < strength_; ++j) {
      local_index += value_indices[j] * mults[j];
    }
    return !covered_.Test(combination_offsets_[ci] + local_index);
  }
  return false;
}

bool CoverageEngine::FirstUncovered(UncoveredAssignment& out) const {
  const uint32_t start = ScanPosition();
  std::vector<uint32_t> radixes(strength_);
  std::vector<uint32_t> value_indices(strength_);

  while (scan_ci_ < num_combinations_) {
    const uint32_t* combo = Combo(scan_ci_);
    uint32_t product = 1;
    for (uint32_t j = 0; j < strength_; ++j) {
      radixes[j] = Parameters()[combo[j]].size();
      product *= radixes[j];
    }

    while (scan_vi_ < product) {
      const uint32_t global_index = combination_offsets_[scan_ci_] + scan_vi_;
      if (covered_.Test(global_index)) {
        ++scan_vi_;
        continue;
      }
      util::DecodeMixedRadix(scan_vi_, radixes, value_indices);
      out.index = global_index;
      out.assignment.assign(Parameters().size(), model::kUnassigned);
      for (uint32_t j = 0; j < strength_; ++j) {
        out.assignment[combo[j]] = value_indices[j];
      }
      // Every index the cursor advanced past was tested and found covered; the
      // tuple the cursor stops on accounts for the one remaining bit test.
      scan_bit_tests_ += (global_index - start) + 1;
      return true;
    }

    ++scan_ci_;
    scan_vi_ = 0;
  }

  scan_bit_tests_ += total_tuples_ - start;
  return false;
}

void CoverageEngine::ExcludeTuple(uint32_t index) {
  if (!covered_.Test(index)) {
    covered_.Set(index);
    ++invalid_tuples_;
    ++covered_bits_;
  }
}

void CoverageEngine::ExcludeInvalidTuples(const std::vector<model::Constraint>& constraints,
                                          const std::vector<std::vector<bool>>& allowed_values,
                                          bool* budget_exceeded) {
  if (constraints.empty()) return;

  uint32_t num_params = static_cast<uint32_t>(Parameters().size());
  model::TestCase witness;
  witness.values.assign(num_params, model::kUnassigned);
  auto parameter_order = allowed_values.empty()
                             ? BuildValidSolveParameterOrder(Parameters())
                             : BuildAllowedSolveParameterOrder(Parameters(), allowed_values);

  ForEachTupleUntil([&](uint32_t global_index, const uint32_t* combo,
                        const std::vector<uint32_t>& value_indices) {
    // Build partial assignment with only this tuple's parameters set.
    for (uint32_t j = 0; j < strength_; ++j) {
      witness.values[combo[j]] = value_indices[j];
    }

    // A tuple belongs to the coverage universe only when it can be extended to
    // a complete assignment using valid values. Evaluating the partial tuple
    // alone is insufficient for interacting implications. Each per-tuple search
    // is bounded; an exhausted budget stops exclusion so the caller can error
    // out rather than proceed on a partially classified universe.
    SolveBudget tuple_budget;
    bool invalid = allowed_values.empty()
                       ? !CompleteValidAssignment(Parameters(), constraints, witness, &tuple_budget,
                                                  &parameter_order)
                       : !CompleteAssignment(Parameters(), constraints, allowed_values, witness,
                                             &tuple_budget, &parameter_order);

    // Reset assignment for reuse.
    std::fill(witness.values.begin(), witness.values.end(), model::kUnassigned);

    if (tuple_budget.exceeded) {
      if (budget_exceeded != nullptr) *budget_exceeded = true;
      return false;  // Stop; universe is not fully classified.
    }

    if (invalid && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
      ++covered_bits_;
    }
    return true;
  });
}

void CoverageEngine::ExcludeInvalidValues() {
  if (!model::HasInvalidValues(Parameters())) return;

  ForEachTuple([&](uint32_t global_index, const uint32_t* combo,
                   const std::vector<uint32_t>& value_indices) {
    // Check if any decoded value is invalid.
    bool contains_invalid = false;
    for (size_t j = 0; j < strength_; ++j) {
      if (Parameters()[combo[j]].is_invalid(value_indices[j])) {
        contains_invalid = true;
        break;
      }
    }

    if (contains_invalid && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
      ++covered_bits_;
    }
  });
}

void CoverageEngine::ExcludeTuplesOutsideMask(
    const std::vector<std::vector<bool>>& allowed_values) {
  if (allowed_values.size() != Parameters().size()) return;
  ForEachTuple([&](uint32_t global_index, const uint32_t* combo,
                   const std::vector<uint32_t>& value_indices) {
    bool excluded = false;
    for (size_t j = 0; j < strength_; ++j) {
      uint32_t pi = combo[j];
      uint32_t vi = value_indices[j];
      if (allowed_values[pi].size() != Parameters()[pi].size() || !allowed_values[pi][vi]) {
        excluded = true;
        break;
      }
    }
    if (excluded && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
      ++covered_bits_;
    }
  });
}

void CoverageEngine::ExcludeTuplesNotContaining(uint32_t param_index, uint32_t value_index) {
  ForEachTuple([&](uint32_t global_index, const uint32_t* combo,
                   const std::vector<uint32_t>& value_indices) {
    bool contains = false;
    for (size_t j = 0; j < strength_; ++j) {
      if (combo[j] == param_index && value_indices[j] == value_index) {
        contains = true;
        break;
      }
    }
    if (!contains && !covered_.Test(global_index)) {
      covered_.Set(global_index);
      ++invalid_tuples_;
      ++covered_bits_;
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
