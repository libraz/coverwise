/// @file options_validation_test.cpp
/// @brief The acceptance gate: what it accepts, and what cannot bypass it.

#include "model/options_validation.h"

#include <gtest/gtest.h>

#include <string>
#include <type_traits>
#include <vector>

#include "model/boundary.h"
#include "model/limits.h"
#include "model/parameter.h"
#include "model/test_case.h"

namespace {

using coverwise::model::AcceptedOptions;
using coverwise::model::AcceptOptions;
using coverwise::model::AggregateBudgetExceededMessage;
using coverwise::model::BoundaryConfig;
using coverwise::model::ChargedString;
using coverwise::model::ChargedStringContext;
using coverwise::model::ChargedText;
using coverwise::model::ChargedTextReader;
using coverwise::model::Error;
using coverwise::model::GenerateOptions;
using coverwise::model::Parameter;
using coverwise::model::ResolveValueName;
using coverwise::model::StringBudgetExceededMessage;
using coverwise::model::ValidatedOptions;

/// @brief A surface's reading of @p bytes of caller text, as the gate takes it.
///
/// The gate takes a total rather than a number so that no call path can supply
/// one without saying where it came from; a test standing in for a surface says
/// so here rather than reaching past the reader.
ChargedText Charged(size_t bytes) {
  ChargedTextReader reader;
  reader.Charge(bytes);
  return reader.total();
}

// The point of the gate is not that validation happens to be called, but that
// there is no way to end up with a ValidatedOptions without calling it. A
// GenerateOptions can be built by anyone — surfaces have to build one — so the
// guarantee has to sit on the type the engine call sites take. If a later edit
// drops the AcceptOptions call, these are the assertions that stop compiling.
static_assert(!std::is_default_constructible_v<ValidatedOptions>,
              "ValidatedOptions must not be constructible without the gate");
static_assert(!std::is_constructible_v<ValidatedOptions, GenerateOptions>,
              "ValidatedOptions must not be constructible from raw options");
static_assert(!std::is_constructible_v<ValidatedOptions, const GenerateOptions&>,
              "ValidatedOptions must not be constructible from raw options");
static_assert(!std::is_constructible_v<AcceptedOptions, Error>,
              "An acceptance verdict must not be fabricated from an error");
static_assert(!std::is_default_constructible_v<AcceptedOptions>,
              "An acceptance verdict must not be fabricated");
static_assert(
    std::is_same_v<decltype(std::declval<const ValidatedOptions&>().get()), const GenerateOptions&>,
    "Validated options must be readable, and only readable");

GenerateOptions TwoBinaryParameters() {
  GenerateOptions options;
  options.parameters.emplace_back("a", std::vector<std::string>{"0", "1"});
  options.parameters.emplace_back("b", std::vector<std::string>{"0", "1"});
  return options;
}

TEST(OptionsGateTest, AcceptsAWellFormedModel) {
  auto accepted = AcceptOptions(TwoBinaryParameters(), ChargedText::None());
  ASSERT_TRUE(accepted.ok()) << accepted.error().message;
  EXPECT_EQ(accepted->get().parameters.size(), 2u);
}

// A weights key is a value name the caller wrote, so the gate reads it the way
// every other caller-written name is read, and stops reading at ASCII.
TEST(OptionsGateTest, AWeightKeyNamesItsValueInAnyAsciiCaseAndNoFurther) {
  GenerateOptions folded;
  folded.parameters.emplace_back("os", std::vector<std::string>{"Windows", "Linux"});
  folded.parameters.emplace_back("b", std::vector<std::string>{"0", "1"});
  folded.weights.entries["os"]["wINdows"] = 5.0;
  auto accepted = AcceptOptions(std::move(folded), ChargedText::None());
  EXPECT_TRUE(accepted.ok()) << accepted.error().message;

  GenerateOptions unfolded;
  unfolded.parameters.emplace_back("city", std::vector<std::string>{"MÜNCHEN", "OSAKA"});
  unfolded.parameters.emplace_back("b", std::vector<std::string>{"0", "1"});
  unfolded.weights.entries["city"]["MüNCHEN"] = 5.0;
  auto refused = AcceptOptions(std::move(unfolded), ChargedText::None());
  ASSERT_FALSE(refused.ok());
  EXPECT_EQ(refused.error().code, Error::Code::kInvalidInput);
  EXPECT_EQ(refused.error().message, "Unknown value in weights: city=MüNCHEN");
}

// Two weights keys naming one value carry two weights for it. Only one can
// apply, and with no declared spelling among them nothing decides which except
// the order the caller's map is walked in — which is sorted in the core and
// insertion-ordered in a JavaScript object. The model is refused rather than
// weighted differently depending on where it ran.
TEST(OptionsGateTest, TwoWeightKeysNamingOneValueAreRefusedUnlessOneIsTheDeclaredSpelling) {
  struct Case {
    const char* label;
    std::vector<std::string> values;
    std::vector<std::string> aliases;
    std::vector<std::string> keys;
    std::string message;  // Empty when the model is accepted.
  };

  const std::vector<Case> cases{
      {"two spellings neither of which is declared",
       {"Windows", "Linux"},
       {},
       {"wINdows", "WINDOWS"},
       "Ambiguous value in weights: p=WINDOWS and p=wINdows name the same value"},
      {"the declared spelling beside a folded one",
       {"Windows", "Linux"},
       {},
       {"Windows", "wINdows"},
       ""},
      {"the declared spelling beside one of its aliases",
       {"Chromium", "Firefox"},
       {"Chrome", "Edge"},
       {"Chromium", "Chrome"},
       ""},
      {"two aliases of one value",
       {"Chromium", "Firefox"},
       {"Chrome", "Edge"},
       {"Chrome", "Edge"},
       "Ambiguous value in weights: p=Chrome and p=Edge name the same value"},
      {"an alias beside a folded spelling of that alias",
       {"Chromium", "Firefox"},
       {"Chrome", "Edge"},
       {"Chrome", "cHROME"},
       "Ambiguous value in weights: p=Chrome and p=cHROME name the same value"},
      {"two keys naming two different values", {"Windows", "Linux"}, {}, {"wINdows", "LINUX"}, ""},
  };

  for (const auto& test : cases) {
    GenerateOptions options;
    Parameter param{"p", test.values};
    if (!test.aliases.empty()) {
      std::vector<std::vector<std::string>> per_value(test.values.size());
      per_value[0] = test.aliases;
      param.set_aliases(per_value);
    }
    options.parameters.push_back(std::move(param));
    options.parameters.emplace_back("q", std::vector<std::string>{"0", "1"});
    for (const auto& key : test.keys) {
      options.weights.entries["p"][key] = 2.0;
    }

    auto accepted = AcceptOptions(std::move(options), ChargedText::None());
    if (test.message.empty()) {
      EXPECT_TRUE(accepted.ok()) << test.label << ": " << accepted.error().message;
    } else {
      ASSERT_FALSE(accepted.ok()) << test.label;
      EXPECT_EQ(accepted.error().code, Error::Code::kInvalidInput) << test.label;
      EXPECT_EQ(accepted.error().message, test.message) << test.label;
    }
  }
}

TEST(OptionsGateTest, RejectionCarriesTheReasonAndNoOptions) {
  GenerateOptions options = TwoBinaryParameters();
  options.strength = 5;
  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().code, Error::Code::kInvalidInput);
  EXPECT_EQ(accepted.error().message, "Strength must be between 1 and parameter count");
}

// A boundary parameter may spell out only the values it wants marked invalid and
// leave the valid ones to the range. Judging the declared list would call that
// model valueless; judging the expanded list is what makes it well-formed.
TEST(OptionsGateTest, AcceptsABoundaryParameterWhoseOnlyDeclaredValueIsInvalid) {
  GenerateOptions options;
  options.parameters.emplace_back("age", std::vector<std::string>{"999"}, std::vector<bool>{true});
  options.parameters.emplace_back("mode", std::vector<std::string>{"a", "b"});
  options.boundary_configs["age"] = {BoundaryConfig::Type::kInteger, 0, 10, 1.0};

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_TRUE(accepted.ok()) << accepted.error().message;
  const auto& age = accepted->get().parameters[0];
  EXPECT_EQ(age.values, (std::vector<std::string>{"-1", "0", "1", "9", "10", "11", "999"}));
  EXPECT_EQ(age.valid_count(), 6u);
  EXPECT_TRUE(age.is_invalid(ResolveValueName(age, "999")));
  EXPECT_TRUE(accepted->get().boundary_configs.empty());
}

// Integer expansion steps by one, so accepting any other step would generate a
// value set the caller did not ask for.
TEST(OptionsGateTest, RejectsAnIntegerBoundaryStepOtherThanOne) {
  GenerateOptions options;
  options.parameters.emplace_back("n", std::vector<std::string>{});
  options.parameters.emplace_back("m", std::vector<std::string>{"a", "b"});
  options.boundary_configs["n"] = {BoundaryConfig::Type::kInteger, 0, 10, 5.0};

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, "Integer boundary step must be 1 for parameter n");
}

TEST(OptionsGateTest, AcceptsAnIntegerBoundaryStepOfOne) {
  GenerateOptions options;
  options.parameters.emplace_back("n", std::vector<std::string>{});
  options.parameters.emplace_back("m", std::vector<std::string>{"a", "b"});
  options.boundary_configs["n"] = {BoundaryConfig::Type::kInteger, 0, 10, 1.0};

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_TRUE(accepted.ok()) << accepted.error().message;
  EXPECT_EQ(accepted->get().parameters[0].values,
            (std::vector<std::string>{"-1", "0", "1", "9", "10", "11"}));
}

TEST(OptionsGateTest, RejectsMoreValuesThanOneParameterMayDeclare) {
  GenerateOptions options = TwoBinaryParameters();
  std::vector<std::string> values;
  values.reserve(coverwise::model::kMaxValuesPerParameter + 1);
  for (size_t i = 0; i <= coverwise::model::kMaxValuesPerParameter; ++i) {
    values.push_back(std::to_string(i));
  }
  options.parameters.emplace_back("wide", std::move(values));

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_NE(accepted.error().message.find("has too many values"), std::string::npos)
      << accepted.error().message;
}

TEST(OptionsGateTest, RejectsMoreConstraintsThanOneModelMayCarry) {
  GenerateOptions options = TwoBinaryParameters();
  for (size_t i = 0; i <= coverwise::model::kMaxConstraints; ++i) {
    options.constraint_expressions.push_back("a = 0");
  }

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_NE(accepted.error().message.find("exceeds maximum"), std::string::npos)
      << accepted.error().message;
}

// The documented bound on a whole input is the aggregate string budget, and it
// is charged over the model rather than over any surface's byte stream.
TEST(OptionsGateTest, RejectsStringDataBeyondTheAggregateBudget) {
  GenerateOptions options = TwoBinaryParameters();
  std::vector<std::string> values;
  const size_t value_bytes = coverwise::model::kMaxStringBytes;
  const size_t value_count = coverwise::model::kMaxAggregateStringBytes / value_bytes + 2;
  for (size_t i = 0; i < value_count; ++i) {
    values.push_back(std::string(value_bytes - 8, 'x') + std::to_string(1000000 + i));
  }
  options.parameters.emplace_back("wide", std::move(values));

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, AggregateBudgetExceededMessage());
}

namespace {

/// @brief A model carrying more unresolved row text than the budget allows.
GenerateOptions ModelWithOversizedRowText() {
  GenerateOptions options = TwoBinaryParameters();
  const size_t row_count = 40;
  const size_t fields_per_row = 2;
  const size_t field_bytes = 60 * 1024;
  EXPECT_GT(row_count * fields_per_row * field_bytes, coverwise::model::kMaxAggregateStringBytes);
  for (size_t row = 0; row < row_count; ++row) {
    coverwise::model::TestCase recorded;
    recorded.values.assign(fields_per_row, coverwise::model::kUnassigned);
    recorded.unresolved.assign(fields_per_row, std::string(field_bytes, 'x'));
    options.seeds.push_back(std::move(recorded));
  }
  return options;
}

}  // namespace

// The two spellings of a charged total are two accounting regimes, and between
// them every string is charged exactly once. These are the two halves of that,
// asked of one model: which of them is named decides whether the gate still has
// the caller's row text to charge.
//
// A caller that counted nothing has counted nothing, and this is the only path
// with no reader in front of it. Leaving its row text uncharged would put the
// embedding entry outside the published budget while every other surface is
// held to it.
TEST(OptionsGateTest, ChargesRowTextWhenNoReaderCountedIt) {
  auto accepted = AcceptOptions(ModelWithOversizedRowText(), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, AggregateBudgetExceededMessage());
}

// A surface that read the row already counted that text as it read it. Charging
// it again where the diagnostics keep it would cost a caller twice for text
// they wrote once, halving the published budget for anyone whose suite has
// drifted from their model.
TEST(OptionsGateTest, DoesNotChargeRowTextAReaderAlreadyCounted) {
  ChargedTextReader reader;
  auto accepted = AcceptOptions(ModelWithOversizedRowText(), reader.total());
  EXPECT_TRUE(accepted.ok()) << accepted.error().message;
}

// The per-string limit reaches that text under the same regime, in the wording
// every other charged kind uses, naming the row it came from.
TEST(OptionsGateTest, NamesTheRowATooLargeUnresolvedValueCameFrom) {
  GenerateOptions options = TwoBinaryParameters();
  options.seeds.push_back(coverwise::model::TestCase{{0, 1}});
  coverwise::model::TestCase drifted;
  drifted.values = {coverwise::model::kUnassigned, 1};
  drifted.unresolved = {std::string(coverwise::model::kMaxStringBytes + 1, 'x'), ""};
  options.seeds.push_back(std::move(drifted));

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, StringBudgetExceededMessage(ChargedStringContext(
                                          ChargedString::kRowValue, {"seeds", 1})));
}

// A row that resolved carries no caller text, so recording rows does not itself
// spend the budget the model's own strings are charged against.
TEST(OptionsGateTest, ResolvedRowsSpendNothingOfTheAggregateBudget) {
  GenerateOptions options = TwoBinaryParameters();
  for (size_t row = 0; row < coverwise::model::kMaxTests; ++row) {
    options.seeds.push_back(coverwise::model::TestCase{{0, 1}});
  }

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  EXPECT_TRUE(accepted.ok()) << accepted.error().message;
}

// The aggregate limit is per input, not per accumulator. A surface that reads
// caller text the options do not carry to the engine — a test row arrives as
// value names and reaches the engine as indices — charges those bytes itself
// and hands the total in, so the two halves are judged against one budget.
TEST(OptionsGateTest, ChargedBytesShareTheBudgetWithTheModelStrings) {
  // Fifteen values of 60 KiB: each is inside the per-string limit, and together
  // they are most of the aggregate one.
  constexpr size_t kValueBytes = 60 * 1024;
  constexpr size_t kValueCount = 15;
  constexpr size_t kHalf = kValueBytes * kValueCount;
  static_assert(kValueBytes <= coverwise::model::kMaxStringBytes);
  static_assert(2 * kHalf > coverwise::model::kMaxAggregateStringBytes);
  auto model_of = [](size_t bytes) {
    GenerateOptions options = TwoBinaryParameters();
    std::vector<std::string> values;
    for (size_t index = 0; index < kValueCount; ++index) {
      values.push_back(std::string(bytes / kValueCount - 8, 'x') + std::to_string(1000000 + index));
    }
    options.parameters.emplace_back("wide", std::move(values));
    return options;
  };

  // Each half fits on its own.
  EXPECT_TRUE(AcceptOptions(model_of(kHalf), ChargedText::None()).ok());
  EXPECT_TRUE(AcceptOptions(TwoBinaryParameters(), Charged(kHalf)).ok());

  // Their sum does not, and says so in the one sentence this limit has.
  auto accepted = AcceptOptions(model_of(kHalf), Charged(kHalf));
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().code, Error::Code::kInvalidInput);
  EXPECT_EQ(accepted.error().message, AggregateBudgetExceededMessage());
}

// A caller's own total can exhaust the budget before the model is walked, and
// the answer must not depend on there being anything left to charge.
TEST(OptionsGateTest, ChargedBytesAloneCanExhaustTheBudget) {
  auto accepted =
      AcceptOptions(TwoBinaryParameters(), Charged(coverwise::model::kMaxAggregateStringBytes + 1));
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, AggregateBudgetExceededMessage());
}

// Reading nothing and reading nothing yet are the same total, so a surface with
// no caller text of its own is judged exactly as one whose reader stayed empty.
TEST(OptionsGateTest, ReadingNoTextMatchesAReaderThatCountedNone) {
  auto plain = AcceptOptions(TwoBinaryParameters(), ChargedText::None());
  auto charged = AcceptOptions(TwoBinaryParameters(), Charged(0));
  ASSERT_TRUE(plain.ok()) << plain.error().message;
  ASSERT_TRUE(charged.ok()) << charged.error().message;
  EXPECT_EQ(charged->get().parameters.size(), plain->get().parameters.size());

  GenerateOptions rejected = TwoBinaryParameters();
  rejected.strength = 5;
  GenerateOptions same = rejected;
  EXPECT_EQ(AcceptOptions(std::move(rejected), ChargedText::None()).error().message,
            AcceptOptions(std::move(same), Charged(0)).error().message);
}

// A caller's total is the only way bytes reach the gate from outside it, and it
// cannot be spelled by accident: ChargedText has no public constructor, so a
// call site that meant to say "nothing to declare" has to say it.
static_assert(!std::is_default_constructible_v<ChargedText>,
              "A charged total must not be produced without naming where it came from");
static_assert(!std::is_constructible_v<ChargedText, size_t>,
              "A charged total must not be produced from a bare byte count");
static_assert(!std::is_invocable_v<decltype(&AcceptOptions), GenerateOptions>,
              "The gate must not accept options without a charged total");

// The per-string refusal names the string it refused, in the one wording the
// limit has. A test that spelled the sentence out would be another copy of it.
TEST(OptionsGateTest, RejectsASingleStringBeyondThePerStringBudget) {
  GenerateOptions options = TwoBinaryParameters();
  options.parameters.emplace_back(
      "wide", std::vector<std::string>{std::string(coverwise::model::kMaxStringBytes + 1, 'x')});

  auto accepted = AcceptOptions(std::move(options), ChargedText::None());
  ASSERT_FALSE(accepted.ok());
  EXPECT_EQ(accepted.error().message, StringBudgetExceededMessage(ChargedStringContext(
                                          ChargedString::kParameterValue, {"wide", 0})));
}

// Every kind of model string is refused by the same generator with its own
// context, so a caller matching on the sentence matches on all of them and a
// wording changed for one kind cannot quietly stay changed for that kind alone.
TEST(OptionsGateTest, NamesEveryKindOfModelStringInOneWording) {
  const std::string oversized(coverwise::model::kMaxStringBytes + 1, 'z');

  auto with_parameter_name = TwoBinaryParameters();
  with_parameter_name.parameters.emplace_back(oversized, std::vector<std::string>{"7"});

  auto with_alias = TwoBinaryParameters();
  with_alias.parameters[1].set_aliases({{}, {oversized}});

  auto with_class = TwoBinaryParameters();
  with_class.parameters[1].set_equivalence_classes({"first", oversized});

  auto with_constraint = TwoBinaryParameters();
  with_constraint.constraint_expressions.push_back(oversized);

  auto with_sub_model = TwoBinaryParameters();
  with_sub_model.sub_models.push_back({{oversized}, 1});

  auto with_weight_parameter = TwoBinaryParameters();
  with_weight_parameter.weights.entries[oversized]["0"] = 2.0;

  auto with_weight_value = TwoBinaryParameters();
  with_weight_value.weights.entries["b"][oversized] = 2.0;

  std::vector<std::pair<GenerateOptions, std::string>> by_kind;
  by_kind.emplace_back(std::move(with_parameter_name),
                       ChargedStringContext(ChargedString::kParameterName, {oversized}));
  by_kind.emplace_back(std::move(with_alias),
                       ChargedStringContext(ChargedString::kValueAlias, {"b", 1}));
  by_kind.emplace_back(std::move(with_class),
                       ChargedStringContext(ChargedString::kEquivalenceClass, {"b", 1}));
  by_kind.emplace_back(std::move(with_constraint),
                       ChargedStringContext(ChargedString::kConstraintExpression, {}));
  by_kind.emplace_back(std::move(with_sub_model),
                       ChargedStringContext(ChargedString::kSubModelParameterName, {}));
  by_kind.emplace_back(std::move(with_weight_parameter),
                       ChargedStringContext(ChargedString::kWeightParameterName, {}));
  by_kind.emplace_back(std::move(with_weight_value),
                       ChargedStringContext(ChargedString::kWeightValueName, {}));

  for (auto& [options, context] : by_kind) {
    auto accepted = AcceptOptions(std::move(options), ChargedText::None());
    ASSERT_FALSE(accepted.ok()) << context;
    EXPECT_EQ(accepted.error().message, StringBudgetExceededMessage(context)) << context;
  }
}

// ExpandBoundaries is exposed so a surface can resolve rows against the final
// value list. Running it twice must land in the same place as running it once,
// which is what makes the split a convenience rather than a way past the gate.
TEST(OptionsGateTest, ExpandingBeforeTheGateChangesNothing) {
  GenerateOptions pre_expanded;
  pre_expanded.parameters.emplace_back("n", std::vector<std::string>{});
  pre_expanded.parameters.emplace_back("m", std::vector<std::string>{"a", "b"});
  pre_expanded.boundary_configs["n"] = {BoundaryConfig::Type::kInteger, 0, 10, 1.0};
  GenerateOptions direct = pre_expanded;

  ASSERT_TRUE(coverwise::model::ExpandBoundaries(pre_expanded).ok());
  auto from_expanded = AcceptOptions(std::move(pre_expanded), ChargedText::None());
  auto from_direct = AcceptOptions(std::move(direct), ChargedText::None());

  ASSERT_TRUE(from_expanded.ok()) << from_expanded.error().message;
  ASSERT_TRUE(from_direct.ok()) << from_direct.error().message;
  EXPECT_EQ(from_expanded->get().parameters[0].values, from_direct->get().parameters[0].values);
}

}  // namespace
