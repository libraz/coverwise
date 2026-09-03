// Exercises the surface the C++ API reference documents, reached through the
// one header that reference tells embedders to include.
#include <coverwise.h>

#include <string>
#include <utility>
#include <vector>

int main() {
  using namespace coverwise;

  model::GenerateOptions options;
  options.parameters = {
      {"os", {"Windows", "macOS", "Linux"}},
      {"browser", {"Chrome", "Firefox", "Safari"}},
      {"size", {"1", "2"}},
  };
  options.constraint_expressions = {"IF os = Windows THEN browser != Safari"};
  options.strength = 2;
  options.seed = 42;
  options.max_tests = 0;
  options.sub_models.push_back(model::SubModel{{"os", "browser"}, 2});
  options.weights.entries["os"]["Linux"] = 2.0;

  model::BoundaryConfig boundary;
  boundary.type = model::BoundaryConfig::Type::kInteger;
  boundary.min_value = 1;
  boundary.max_value = 2;
  options.boundary_configs["size"] = boundary;

  const model::ModelStats stats = core::EstimateModel(options);
  if (!stats.error.ok() || stats.parameter_count == 0) return 1;

  const model::GenerateResult result = core::Generate(options);
  if (!result.error.ok() || result.coverage != 1.0) return 1;

  std::vector<model::Constraint> constraints;
  const model::ParseOptions parse_options;
  for (const auto& expression : options.constraint_expressions) {
    model::ParseResult parsed =
        model::ParseConstraint(expression, result.parameters, parse_options);
    if (!parsed.error.ok()) return 1;
    constraints.push_back(std::move(parsed.constraint));
  }

  const validator::ConstraintReport constraint_report =
      validator::ValidateConstraints(result.tests, constraints);
  if (constraint_report.violations != 0) return 1;

  const validator::CoverageReport coverage_report =
      validator::ValidateCoverage(result.parameters, result.tests, options.strength, constraints);
  if (!coverage_report.error.ok() || coverage_report.coverage_ratio != 1.0) return 1;

  const validator::ClassCoverageReport class_report =
      validator::ComputeClassCoverage(result.parameters, result.tests, options.strength);
  if (!class_report.error.ok()) return 1;

  const std::vector<model::TestCase> existing(result.tests.begin(), result.tests.begin() + 1);
  const model::GenerateResult extended = core::Extend(existing, options);
  if (!extended.error.ok() || extended.tests.size() < existing.size()) return 1;

  const std::string first = result.parameters.front().name;
  return first.empty() ? 1 : 0;
}
