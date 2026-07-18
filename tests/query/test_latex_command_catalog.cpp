#include "support/folia_test.hpp"

import folia.core.latex_command_catalog;

using namespace boost::ut;
using namespace folia;

namespace {

LatexCommandDefinition Command(
    std::string id,
    std::u32string trigger,
    std::u32string snippet = U"x") {
    return {
        .id = std::move(id),
        .trigger = std::move(trigger),
        .snippet = std::move(snippet),
    };
}

} // namespace

suite latex_command_catalog_tests = [] {

"custom command mutations are validated without partially changing state"_test = [] {
    LatexCommandCatalogState catalog{{Command("builtin.frac", U"frac")}, {}};
    auto added = catalog.AddCustom(U"\\frac", U"\\dfrac{$1}{$2}$0", "Fraction");
    expect(added.ok());
    expect(added.command_id == "user.frac");
    expect(catalog.CustomCommands().size() == 1_u);
    expect(catalog.Commands().size() == 1_u);
    expect(catalog.Commands().front().id == "user.frac");

    auto duplicate = catalog.AddCustom(U"frac", U"other", "Duplicate");
    expect(duplicate.error == LatexCatalogMutationError::DuplicateCustomTrigger);
    auto invalid = catalog.UpdateCustom("user.frac", U"fr ac", U"other", "Invalid");
    expect(invalid.error == LatexCatalogMutationError::InvalidDefinition);
    expect(catalog.CustomCommands().front().trigger == U"frac");
    expect(catalog.CustomCommands().front().snippet == U"\\dfrac{$1}{$2}$0");
};

"custom ids remain unique when loaded data already owns the natural id"_test = [] {
    auto existing = Command("user.sqrt", U"root");
    existing.built_in = false;
    LatexCommandCatalogState catalog{{}, {existing}};
    auto added = catalog.AddCustom(U"sqrt", U"\\sqrt{$1}$0", "Square root");
    expect(added.ok());
    expect(added.command_id == "user.sqrt.2");
};

"removing a custom command removes its usage while missing commands are harmless"_test = [] {
    auto custom = Command("user.alpha", U"alpha");
    custom.built_in = false;
    std::unordered_map<std::string, LatexCommandUsage> usage{
        {"user.alpha", {.score = 3.0, .last_used_epoch_seconds = 100}},
    };
    LatexCommandCatalogState catalog{{}, {custom}, usage};
    auto missing = catalog.RemoveCustom("builtin.alpha");
    expect(missing.error == LatexCatalogMutationError::MissingCustomCommand);
    expect(catalog.Usage().contains("user.alpha"));
    expect(catalog.RemoveCustom("user.alpha").ok());
    expect(catalog.CustomCommands().empty());
    expect(!catalog.Usage().contains("user.alpha"));
};

"usage recording and reset are deterministic and ignore unknown ids"_test = [] {
    LatexCommandCatalogState catalog{{Command("symbol.alpha", U"alpha")}, {}};
    expect(!catalog.RecordUsage("missing", 100));
    expect(catalog.RecordUsage("symbol.alpha", 100));
    expect(catalog.RecentScore("symbol.alpha", 100) > 0.99);
    auto query = catalog.QueryAt(U"\\a", 2, 100);
    expect(fatal(query.has_value()));
    expect(query->candidates.front().command.id == "symbol.alpha");
    catalog.ResetUsage();
    expect(catalog.Usage().empty());
    expect(catalog.RecentScore("symbol.alpha", 100) == 0.0);
};

};
