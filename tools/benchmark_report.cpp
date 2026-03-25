#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "core/generator.h"
#include "model/generate_options.h"
#include "model/parameter.h"
#include "validator/coverage_validator.h"

using coverwise::core::Generate;
using coverwise::model::GenerateOptions;
using coverwise::model::GenerateResult;
using coverwise::model::Parameter;
using coverwise::validator::CoverageReport;
using coverwise::validator::ValidateCoverage;

namespace {

std::vector<Parameter> MakeUniformParams(uint32_t count, uint32_t values_per_param) {
  std::vector<Parameter> params;
  params.reserve(count);
  for (uint32_t i = 0; i < count; ++i) {
    std::vector<std::string> values;
    values.reserve(values_per_param);
    for (uint32_t j = 0; j < values_per_param; ++j) {
      values.push_back("v" + std::to_string(j));
    }
    params.emplace_back("P" + std::to_string(i), std::move(values));
  }
  return params;
}

struct BenchmarkConfig {
  std::string name;
  GenerateOptions opts;
  uint32_t expected_tuples;
  uint32_t lower_bound;
  uint32_t upper_bound;
};

std::vector<BenchmarkConfig> BuildConfigs() {
  std::vector<BenchmarkConfig> configs;

  // Uniform configs
  auto add_uniform = [&](const std::string& name, uint32_t params, uint32_t vals,
                         uint32_t tuples, uint32_t lo, uint32_t hi) {
    GenerateOptions opts;
    opts.parameters = MakeUniformParams(params, vals);
    opts.strength = 2;
    opts.seed = 42;
    configs.push_back({name, std::move(opts), tuples, lo, hi});
  };

  // Core benchmarks (with known theoretical bounds)
  add_uniform("5x3 uniform", 5, 3, 90, 9, 18);
  add_uniform("10x3 uniform", 10, 3, 405, 9, 25);
  add_uniform("13x3 uniform", 13, 3, 702, 9, 30);
  add_uniform("10x5 uniform", 10, 5, 1125, 25, 55);
  add_uniform("20x2 uniform", 20, 2, 760, 4, 12);
  add_uniform("15x4 uniform", 15, 4, 1680, 16, 44);

  // Additional scale benchmarks
  add_uniform("20x5 uniform", 20, 5, 4750, 25, 70);
  add_uniform("30x5 uniform", 30, 5, 10875, 25, 80);
  add_uniform("50x3 uniform", 50, 3, 11025, 9, 35);
  add_uniform("5x20 uniform", 5, 20, 4000, 400, 600);

  // Higher-strength benchmarks
  {
    GenerateOptions opts;
    opts.parameters = MakeUniformParams(15, 3);
    opts.strength = 3;
    opts.seed = 42;
    // C(15,3) * 3^3 = 455 * 27 = 12285
    configs.push_back({"15x3 3-wise", std::move(opts), 12285, 27, 150});
  }
  {
    GenerateOptions opts;
    opts.parameters = MakeUniformParams(8, 3);
    opts.strength = 4;
    opts.seed = 42;
    // C(8,4) * 3^4 = 70 * 81 = 5670
    configs.push_back({"8x3 4-wise", std::move(opts), 5670, 81, 350});
  }

  // Mixed: 3^4 * 2^3
  {
    GenerateOptions opts;
    for (uint32_t i = 0; i < 4; ++i) {
      opts.parameters.emplace_back("A" + std::to_string(i),
                                   std::vector<std::string>{"v0", "v1", "v2"});
    }
    for (uint32_t i = 0; i < 3; ++i) {
      opts.parameters.emplace_back("B" + std::to_string(i),
                                   std::vector<std::string>{"v0", "v1"});
    }
    opts.strength = 2;
    opts.seed = 42;
    configs.push_back({"3^4 * 2^3 mixed", std::move(opts), 138, 9, 25});
  }

  // Mixed: 5 * 3^3 * 2^4
  {
    GenerateOptions opts;
    opts.parameters.emplace_back("X0",
                                 std::vector<std::string>{"v0", "v1", "v2", "v3", "v4"});
    for (uint32_t i = 0; i < 3; ++i) {
      opts.parameters.emplace_back("A" + std::to_string(i),
                                   std::vector<std::string>{"v0", "v1", "v2"});
    }
    for (uint32_t i = 0; i < 4; ++i) {
      opts.parameters.emplace_back("B" + std::to_string(i),
                                   std::vector<std::string>{"v0", "v1"});
    }
    opts.strength = 2;
    opts.seed = 42;
    configs.push_back({"5*3^3*2^4 mixed", std::move(opts), 208, 15, 50});
  }

  return configs;
}

}  // namespace

int main() {
  auto configs = BuildConfigs();

  std::printf("%-20s | %6s | %5s | %5s | %5s | %s\n",
              "Configuration", "Tuples", "Tests", "Lower", "Upper", "Status");
  std::printf("%-20s-|-%6s-|-%5s-|-%5s-|-%5s-|-%s\n",
              "--------------------", "------", "-----", "-----", "-----", "------");

  int failures = 0;

  for (auto& cfg : configs) {
    auto t0 = std::chrono::steady_clock::now();
    auto result = Generate(cfg.opts);
    auto t1 = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    auto report = ValidateCoverage(cfg.opts.parameters, result.tests, cfg.opts.strength);

    uint32_t test_count = static_cast<uint32_t>(result.tests.size());
    bool coverage_ok = (report.coverage_ratio == 1.0);
    bool bounds_ok = (test_count >= cfg.lower_bound && test_count <= cfg.upper_bound);

    std::string status;
    if (coverage_ok && bounds_ok) {
      status = "OK";
    } else if (!coverage_ok) {
      status = "COVERAGE FAIL";
      ++failures;
    } else {
      status = "BOUNDS FAIL";
      ++failures;
    }

    std::printf("%-20s | %6u | %5u | %5u | %5u | %s (%ldms)\n",
                cfg.name.c_str(), cfg.expected_tuples, test_count,
                cfg.lower_bound, cfg.upper_bound, status.c_str(),
                static_cast<long>(ms));
  }

  std::printf("\n");
  if (failures == 0) {
    std::printf("All %zu configurations passed.\n", configs.size());
  } else {
    std::printf("%d of %zu configurations FAILED.\n", failures, configs.size());
  }

  return failures > 0 ? 1 : 0;
}
