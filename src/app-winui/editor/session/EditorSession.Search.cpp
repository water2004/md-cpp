#include "pch.h"
#include "editor/session/EditorSession.h"

namespace winrt::Folia
{
    EditorSearchSummary EditorSession::Search(
        std::u32string_view query,
        folia::SearchOptions options)
    {
        ClearSearch();
        if (query.empty()) return {};

        if (core_->sourceEditor)
        {
            auto result = folia::search_text(core_->sourceEditor->source(), query, options);
            if (!result.valid()) return {0, std::move(result.error)};
            core_->searchTargets.reserve(result.matches.size());
            for (auto const& match : result.matches)
            {
                detail::EditorSessionCore::SearchTarget target;
                target.sourceRange = match.range;
                for (auto const& line : core_->sourceEditor->lines())
                {
                    auto const lineStart = line.source_start;
                    auto const lineEnd = line.source_end();
                    auto const start = (std::max)(match.range.start, lineStart);
                    auto const end = (std::min)(match.range.end, lineEnd);
                    if (start < end)
                    {
                        target.spans.push_back({
                            line.id,
                            {start - lineStart, end - lineStart}});
                    }
                }
                if (target.spans.empty())
                {
                    auto position = core_->sourceEditor->position_from_source_offset(
                        match.range.start);
                    target.spans.push_back({
                        position.container_id,
                        {position.source_offset, position.source_offset}});
                }
                core_->searchTargets.push_back(std::move(target));
            }
        }
        else
        {
            auto result = folia::search_rendered_fragments(
                RenderedSearchFragments(), query, options);
            if (!result.valid()) return {0, std::move(result.error)};
            core_->searchTargets.reserve(result.matches.size());
            for (auto const& match : result.matches)
            {
                detail::EditorSessionCore::SearchTarget target;
                target.spans.reserve(match.source_ranges.size());
                for (auto const range : match.source_ranges)
                    target.spans.push_back({match.container_id, range});
                core_->searchTargets.push_back(std::move(target));
            }
        }
        RebuildSearchHighlights();
        return {core_->searchTargets.size(), std::nullopt};
    }

    bool EditorSession::ActivateSearchMatch(std::size_t index)
    {
        if (index >= core_->searchTargets.size()) return false;
        auto const& target = core_->searchTargets[index];
        if (target.spans.empty()) return false;
        core_->activeSearchMatch = index;
        RebuildSearchHighlights();

        if (core_->sourceEditor && target.sourceRange)
        {
            core_->sourceEditor->set_selection({
                target.sourceRange->start,
                target.sourceRange->end,
                folia::TextAffinity::Downstream,
                folia::TextAffinity::Upstream});
        }
        else
        {
            auto const& first = target.spans.front();
            auto const& last = target.spans.back();
            core_->editor.set_selection({
                {first.container_id, first.source_range.start, folia::TextAffinity::Downstream},
                {last.container_id, last.source_range.end, folia::TextAffinity::Upstream}});
        }
        return true;
    }

    bool EditorSession::CommitRenderedSearchReplacement(
        std::span<const folia::RenderedSearchMatch> matches)
    {
        auto const revisionBefore = core_->editor.revision();
        if (!core_->editor.execute_document_replace_matches(matches)) return false;
        if (core_->editor.revision() == revisionBefore) return false;
        ++revision_;
        auto change = core_->editor.take_last_document_change();
        core_->renderedSearchRevision = (std::numeric_limits<std::uint64_t>::max)();
        RefreshCharacterCount();
        RebuildRenderModel(change ? &*change : nullptr);
        ClearSearch();
        return true;
    }

    bool EditorSession::ReplaceSearchMatch(
        std::size_t index,
        std::u32string_view query,
        std::u32string_view replacement,
        folia::SearchOptions options)
    {
        if (query.empty()) return false;
        if (core_->sourceEditor)
        {
            auto result = folia::search_text_for_replacement(
                core_->sourceEditor->source(), query, replacement, options);
            if (!result.valid() || index >= result.matches.size()) return false;
            auto const& match = result.matches[index];
            if (!match.replacement
                || !core_->sourceEditor->replace(match.range, *match.replacement)) return false;
            ++revision_;
            core_->characterCount = core_->sourceEditor->source().size();
            RebuildRenderModel();
            ClearSearch();
            return true;
        }

        auto result = folia::search_rendered_fragments_for_replacement(
            RenderedSearchFragments(), query, replacement, options);
        if (!result.valid() || index >= result.matches.size()) return false;
        return CommitRenderedSearchReplacement(
            std::span<const folia::RenderedSearchMatch>{&result.matches[index], 1});
    }

    bool EditorSession::ReplaceAllSearchMatches(
        std::u32string_view query,
        std::u32string_view replacement,
        folia::SearchOptions options)
    {
        if (query.empty()) return false;
        if (core_->sourceEditor)
        {
            auto result = folia::search_text_for_replacement(
                core_->sourceEditor->source(), query, replacement, options);
            if (!result.valid() || result.matches.empty()) return false;
            std::vector<folia::SourceReplacement> replacements;
            replacements.reserve(result.matches.size());
            for (auto const& match : result.matches)
            {
                if (!match.replacement) return false;
                replacements.push_back({match.range, *match.replacement});
            }
            if (!core_->sourceEditor->replace_all(replacements)) return false;
            ++revision_;
            core_->characterCount = core_->sourceEditor->source().size();
            RebuildRenderModel();
            ClearSearch();
            return true;
        }

        auto result = folia::search_rendered_fragments_for_replacement(
            RenderedSearchFragments(), query, replacement, options);
        if (!result.valid() || result.matches.empty()) return false;
        return CommitRenderedSearchReplacement(result.matches);
    }

    void EditorSession::RebuildSearchHighlights()
    {
        core_->searchHighlights.clear();
        for (std::size_t index = 0; index < core_->searchTargets.size(); ++index)
        {
            auto const current = core_->activeSearchMatch == index;
            for (auto const& span : core_->searchTargets[index].spans)
                core_->searchHighlights.push_back({span, current});
        }
    }

    void EditorSession::ClearSearch()
    {
        if (!core_) return;
        core_->searchTargets.clear();
        core_->searchHighlights.clear();
        core_->activeSearchMatch.reset();
    }

    std::span<const folia::RenderedSearchFragment>
    EditorSession::RenderedSearchFragments()
    {
        auto const revision = core_->editor.revision();
        if (core_->renderedSearchRevision != revision)
        {
            core_->renderedSearchFragments = folia::rendered_search_fragments(
                core_->editor.document());
            core_->renderedSearchRevision = revision;
        }
        return core_->renderedSearchFragments;
    }
}
