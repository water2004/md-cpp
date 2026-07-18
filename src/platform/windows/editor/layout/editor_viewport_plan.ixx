// elmd.platform.editor_viewport_plan — deterministic viewport work bands.
export module elmd.platform.editor_viewport_plan;
import std;
import elmd.platform.editor_geometry;

export namespace elmd::platform::editor
{
    struct EditorIndexRange
    {
        std::size_t begin = 0;
        std::size_t end = 0;

        bool Empty() const noexcept { return begin >= end; }
        std::size_t Size() const noexcept { return end > begin ? end - begin : 0; }
        bool Contains(std::size_t index) const noexcept
        {
            return begin <= index && index < end;
        }
        bool operator==(EditorIndexRange const&) const = default;
    };

    struct EditorViewportPolicy
    {
        float viewportOverscan = 240.0f;
        float embeddedBefore = 1200.0f;
        float embeddedAfter = 800.0f;
        float embeddedUnloadBefore = 2400.0f;
        float embeddedUnloadAfter = 2000.0f;
        float minimumPrefetch = 480.0f;
        float prefetchViewportFactor = 0.75f;
        float minimumRetentionBefore = 2400.0f;
        float retentionBeforeViewportFactor = 2.5f;
        float minimumRetentionAfter = 3200.0f;
        float retentionAfterViewportFactor = 3.0f;
    };

    struct EditorViewportPlan
    {
        EditorIndexRange visible;
        EditorIndexRange prefetch;
        EditorIndexRange embedded;
        EditorIndexRange retention;
        float embeddedKeepTop = 0.0f;
        float embeddedKeepBottom = 0.0f;
    };

    inline EditorIndexRange EditorBlocksIntersecting(
        EditorBlockGeometryIndex const& geometry,
        float documentTop,
        float documentBottom)
    {
        if (!geometry.Initialized() || geometry.Size() == 0) return {};
        documentTop = std::isfinite(documentTop) ? documentTop : 0.0f;
        documentBottom = std::isfinite(documentBottom) ? documentBottom : documentTop;
        if (documentBottom < documentTop) std::swap(documentTop, documentBottom);

        auto begin = geometry.FirstIntersecting(documentTop);
        auto end = begin;
        while (end < geometry.Size() && geometry.At(end).top <= documentBottom) ++end;
        return {begin, end};
    }

    inline EditorViewportPlan BuildEditorViewportPlan(
        EditorBlockGeometryIndex const& geometry,
        float scrollOffset,
        float viewportHeight,
        bool printMode,
        bool scrollingForward,
        EditorViewportPolicy policy = {})
    {
        scrollOffset = std::isfinite(scrollOffset) ? (std::max)(0.0f, scrollOffset) : 0.0f;
        viewportHeight = std::isfinite(viewportHeight) ? (std::max)(0.0f, viewportHeight) : 0.0f;

        auto viewportTop = scrollOffset - policy.viewportOverscan;
        auto viewportBottom = scrollOffset + viewportHeight + policy.viewportOverscan;
        auto visible = EditorBlocksIntersecting(geometry, viewportTop, viewportBottom);

        EditorIndexRange prefetch{visible.end, visible.end};
        if (!printMode)
        {
            auto distance = (std::max)(
                policy.minimumPrefetch,
                viewportHeight * policy.prefetchViewportFactor);
            if (scrollingForward)
            {
                auto forward = EditorBlocksIntersecting(
                    geometry,
                    viewportBottom,
                    viewportBottom + distance);
                prefetch = {visible.end, (std::max)(visible.end, forward.end)};
            }
            else
            {
                auto backward = EditorBlocksIntersecting(
                    geometry,
                    viewportTop - distance,
                    viewportTop);
                prefetch = {(std::min)(backward.begin, visible.begin), visible.begin};
            }
        }

        auto embedded = EditorBlocksIntersecting(
            geometry,
            scrollOffset - policy.embeddedBefore,
            scrollOffset + viewportHeight + policy.embeddedAfter);

        auto retentionBefore = printMode
            ? 1.0f
            : (std::max)(
                policy.minimumRetentionBefore,
                viewportHeight * policy.retentionBeforeViewportFactor);
        auto retentionAfter = printMode
            ? policy.viewportOverscan
            : (std::max)(
                policy.minimumRetentionAfter,
                viewportHeight * policy.retentionAfterViewportFactor);
        auto retention = EditorBlocksIntersecting(
            geometry,
            scrollOffset - retentionBefore,
            scrollOffset + viewportHeight + retentionAfter);

        return {
            .visible = visible,
            .prefetch = prefetch,
            .embedded = embedded,
            .retention = retention,
            .embeddedKeepTop = scrollOffset - policy.embeddedUnloadBefore,
            .embeddedKeepBottom = scrollOffset + viewportHeight + policy.embeddedUnloadAfter,
        };
    }
}
