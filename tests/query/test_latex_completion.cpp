#include "support/folia_test.hpp"

import folia.core.latex_completion;

using namespace boost::ut;
using namespace folia;

namespace {

LatexCommandDefinition Command(
    std::string id,
    std::u32string trigger,
    std::u32string snippet) {
    return {
        .id = std::move(id),
        .trigger = std::move(trigger),
        .snippet = std::move(snippet),
    };
}

} // namespace

suite latex_completion_tests = [] {

"LaTeX prefix query replaces the complete command around the caret"_test = [] {
    auto prefix = latex_command_prefix_at(U"x + \\frac", 7);
    expect(fatal(prefix.has_value()));
    expect(prefix->prefix == U"fr");
    expect(prefix->replacement == SourceRange{4, 9});
};

"LaTeX prefix query accepts an empty prefix after one command slash"_test = [] {
    auto prefix = latex_command_prefix_at(U"\\", 1);
    expect(fatal(prefix.has_value()));
    expect(prefix->prefix.empty());
    expect(prefix->replacement == SourceRange{0, 1});
};

"escaped slash runs do not start a LaTeX command"_test = [] {
    expect(!latex_command_prefix_at(U"\\\\frac", 6).has_value());
    auto prefix = latex_command_prefix_at(U"\\\\\\frac", 7);
    expect(fatal(prefix.has_value()));
    expect(prefix->replacement == SourceRange{2, 7});
};

"invalid offsets and ordinary words do not create completion queries"_test = [] {
    expect(!latex_command_prefix_at(U"frac", 4).has_value());
    expect(!latex_command_prefix_at(U"\\frac", 99).has_value());
};

"recently accepted commands rank ahead within the same prefix"_test = [] {
    std::vector catalog{
        Command("frac", U"frac", U"\\frac{$1}{$2}$0"),
        Command("framebox", U"framebox", U"\\framebox{$1}$0"),
    };
    std::unordered_map<std::string, LatexCommandUsage> usage;
    record_latex_command_usage(usage["framebox"], 1000);
    auto candidates = query_latex_commands(catalog, U"fr", usage, 1000);
    expect(candidates.size() == 2_u);
    expect(candidates[0].command.id == "framebox");
    expect(candidates[1].command.id == "frac");
};

"exact matches outrank recent prefix matches"_test = [] {
    std::vector catalog{
        Command("frac", U"frac", U"\\frac{$1}{$2}$0"),
        Command("fraction", U"fraction", U"\\fraction{$1}$0"),
    };
    std::unordered_map<std::string, LatexCommandUsage> usage;
    for (int index = 0; index < 20; ++index)
        record_latex_command_usage(usage["fraction"], 1000);
    auto candidates = query_latex_commands(catalog, U"frac", usage, 1000);
    expect(candidates.size() == 2_u);
    expect(candidates[0].command.id == "frac");
    expect(candidates[0].exact_match);
};

"usage score decays by half over one half life"_test = [] {
    LatexCommandUsage usage;
    record_latex_command_usage(usage, 100, 20.0);
    auto score = decayed_latex_usage_score(usage, 120, 20.0);
    expect(score > 0.499);
    expect(score < 0.501);
};

"custom commands shadow built-ins by trigger without compatibility entries"_test = [] {
    std::vector built_in{Command("builtin.frac", U"frac", U"\\frac{$1}{$2}$0")};
    std::vector custom{Command("custom.frac", U"\\frac", U"\\dfrac{$1}{$2}$0")};
    auto merged = merge_latex_command_catalog(built_in, custom);
    expect(merged.size() == 1_u);
    expect(merged[0].id == "custom.frac");
    expect(!merged[0].built_in);
};

"disabled and malformed commands are excluded from suggestions"_test = [] {
    auto disabled = Command("disabled", U"frac", U"x");
    disabled.enabled = false;
    std::vector catalog{
        disabled,
        Command("bad", U"fr-ac", U"x"),
        Command("valid", U"frame", U"x"),
    };
    auto candidates = query_latex_commands(
        catalog, U"fr", std::unordered_map<std::string, LatexCommandUsage>{}, 0);
    expect(candidates.size() == 1_u);
    expect(candidates[0].command.id == "valid");
};

};
