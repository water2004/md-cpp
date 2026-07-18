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

std::vector<LatexCommandInvocations> Compile(
    std::vector<LatexCommandDefinition> const& catalog) {
    std::vector<LatexCommandInvocations> result;
    result.reserve(catalog.size());
    for (auto const& command : catalog)
        result.push_back(compile_latex_command_invocations(command));
    return result;
}

std::optional<LatexCompletionQuery> Query(
    std::vector<LatexCommandDefinition> const& catalog,
    std::u32string_view source,
    std::size_t caret,
    std::unordered_map<std::string, LatexCommandUsage> const& usage = {},
    std::int64_t now = 0,
    std::size_t limit = 8) {
    auto invocations = Compile(catalog);
    return query_latex_commands_at(
        catalog, invocations, source, caret, usage, now, limit);
}

} // namespace

suite latex_completion_tests = [] {

"registered snippets compile direct and canonical invocation forms"_test = [] {
    auto matrix = Command(
        "matrix", U"matrix", U"\\begin{matrix}\n$1\n\\end{matrix}$0");
    auto forms = compile_latex_command_invocations(matrix);
    expect(forms == LatexCommandInvocations{U"matrix", U"begin{matrix}"});

    auto fraction = Command("frac", U"frac", U"\\frac{$1}{$2}$0");
    forms = compile_latex_command_invocations(fraction);
    expect(forms == LatexCommandInvocations{U"frac", U"frac{"});
};

"query replaces the complete registered command around the caret"_test = [] {
    std::vector catalog{Command("frac", U"frac", U"\\frac{$1}{$2}$0")};
    auto query = Query(catalog, U"x + \\frac", 7);
    expect(fatal(query.has_value()));
    expect(query->prefix == U"fr");
    expect(query->replacement == SourceRange{4, 9});
    expect(query->candidates.size() == 1_u);
};

"query accepts an empty prefix after one active slash"_test = [] {
    std::vector catalog{Command("frac", U"frac", U"\\frac{$1}{$2}$0")};
    auto query = Query(catalog, U"\\", 1);
    expect(fatal(query.has_value()));
    expect(query->prefix.empty());
    expect(query->replacement == SourceRange{0, 1});
};

"begin completion remains active through braces because the catalog registers it"_test = [] {
    auto matrix = Command(
        "matrix", U"matrix", U"\\begin{matrix}\n$1\n\\end{matrix}$0");
    auto cases = Command(
        "cases", U"cases", U"\\begin{cases}\n$1\n\\end{cases}$0");
    cases.category = "custom";
    std::vector catalog{matrix, cases};

    for (auto source : {U"\\b", U"\\begin", U"\\begin{", U"\\begin{m"}) {
        auto query = Query(catalog, source, std::u32string_view{source}.size());
        expect(fatal(query.has_value()));
    }

    auto partial = Query(catalog, U"x \\begin{matrix} y", 12);
    expect(fatal(partial.has_value()));
    expect(partial->prefix == U"begin{mat");
    expect(partial->replacement == SourceRange{2, 16});
    expect(partial->candidates.size() == 1_u);
    expect(partial->candidates[0].command.id == "matrix");
};

"unregistered begin spelling does not survive punctuation"_test = [] {
    std::vector catalog{Command("beta", U"beta", U"\\beta$0")};
    expect(!Query(catalog, U"\\begin{", 7).has_value());
};

"registered custom punctuation is matched without a LaTeX character whitelist"_test = [] {
    auto custom = Command("custom", U"thing", U"\\operator@name{$1}$0");
    custom.category = "custom";
    std::vector catalog{custom};
    auto query = Query(catalog, U"\\operator@", 10);
    expect(fatal(query.has_value()));
    expect(query->prefix == U"operator@");
    expect(query->candidates.size() == 1_u);
};

"escaped slash runs do not start a registered command"_test = [] {
    std::vector catalog{Command("frac", U"frac", U"\\frac{$1}{$2}$0")};
    expect(!Query(catalog, U"\\\\frac", 6).has_value());
    auto query = Query(catalog, U"\\\\\\frac", 7);
    expect(fatal(query.has_value()));
    expect(query->replacement == SourceRange{2, 7});
};

"invalid offsets and ordinary words do not create completion queries"_test = [] {
    std::vector catalog{Command("frac", U"frac", U"\\frac{$1}{$2}$0")};
    expect(!Query(catalog, U"frac", 4).has_value());
    expect(!Query(catalog, U"\\frac", 99).has_value());
};

"existing arguments are not swallowed when the direct invocation is complete"_test = [] {
    std::vector catalog{Command("frac", U"frac", U"\\frac{$1}{$2}$0")};
    auto query = Query(catalog, U"\\frac{old}", 3);
    expect(fatal(query.has_value()));
    expect(query->prefix == U"fr");
    expect(query->replacement == SourceRange{0, 5});
};

"recently accepted commands rank ahead within the same registered prefix"_test = [] {
    std::vector catalog{
        Command("frac", U"frac", U"\\frac{$1}{$2}$0"),
        Command("framebox", U"framebox", U"\\framebox{$1}$0"),
    };
    std::unordered_map<std::string, LatexCommandUsage> usage;
    record_latex_command_usage(usage["framebox"], 1000);
    auto query = Query(catalog, U"\\fr", 3, usage, 1000);
    expect(fatal(query.has_value()));
    expect(query->candidates.size() == 2_u);
    expect(query->candidates[0].command.id == "framebox");
    expect(query->candidates[1].command.id == "frac");
};

"exact registered forms outrank recent prefix matches"_test = [] {
    std::vector catalog{
        Command("frac", U"frac", U"\\frac{$1}{$2}$0"),
        Command("fraction", U"fraction", U"\\fraction{$1}$0"),
    };
    std::unordered_map<std::string, LatexCommandUsage> usage;
    for (int index = 0; index < 20; ++index)
        record_latex_command_usage(usage["fraction"], 1000);
    auto query = Query(catalog, U"\\frac", 5, usage, 1000);
    expect(fatal(query.has_value()));
    expect(query->candidates.size() == 2_u);
    expect(query->candidates[0].command.id == "frac");
    expect(query->candidates[0].exact_match);
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

"disabled and malformed commands are excluded from registered queries"_test = [] {
    auto disabled = Command("disabled", U"frac", U"x");
    disabled.enabled = false;
    std::vector catalog{
        disabled,
        Command("bad", U"fr-ac", U"x"),
        Command("valid", U"frame", U"\\frame$0"),
    };
    auto query = Query(catalog, U"\\fr", 3);
    expect(fatal(query.has_value()));
    expect(query->candidates.size() == 1_u);
    expect(query->candidates[0].command.id == "valid");
};

};
