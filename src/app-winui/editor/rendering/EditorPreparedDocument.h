#pragma once

#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorTableBlockRenderer.h"

namespace winrt::ElMd
{
    struct EditorSurfaceRenderer::PreparedDocument
    {
        struct GeometryIndex
        {
            struct Entry
            {
                float marginTop = 0.0f;
                float height = 0.0f;
                float trailing = 0.0f;
            };

            struct Placement
            {
                float top = 0.0f;
                float bottom = 0.0f;
            };

            void Reset(std::vector<Entry> values, float documentPadding)
            {
                entries = std::move(values);
                padding = documentPadding;
                fenwick.assign(entries.size() + 1, 0.0f);
                for (std::size_t index = 0; index < entries.size(); ++index)
                {
                    auto treeIndex = index + 1;
                    fenwick[treeIndex] += Extent(entries[index]);
                    auto parent = treeIndex + (treeIndex & (~treeIndex + 1));
                    if (parent < fenwick.size()) fenwick[parent] += fenwick[treeIndex];
                }
                initialized = true;
            }

            bool Initialized() const noexcept { return initialized; }
            std::size_t Size() const noexcept { return entries.size(); }

            Placement At(std::size_t index) const
            {
                if (index >= entries.size()) return {};
                auto const& entry = entries[index];
                auto top = padding + Prefix(index) + entry.marginTop;
                return {top, top + entry.height};
            }

            void UpdateHeight(std::size_t index, float height)
            {
                if (index >= entries.size()) return;
                auto updated = entries[index];
                updated.height = height;
                Update(index, updated);
            }

            void Update(std::size_t index, Entry updated)
            {
                if (index >= entries.size()) return;
                auto delta = Extent(updated) - Extent(entries[index]);
                entries[index] = updated;
                if (delta == 0.0f) return;
                for (auto treeIndex = index + 1; treeIndex < fenwick.size(); treeIndex += treeIndex & (~treeIndex + 1))
                    fenwick[treeIndex] += delta;
            }

            std::size_t FirstIntersecting(float documentTop) const
            {
                std::size_t first = 0;
                auto last = entries.size();
                while (first < last)
                {
                    auto middle = first + (last - first) / 2;
                    if (At(middle).bottom < documentTop) first = middle + 1;
                    else last = middle;
                }
                return first;
            }

            float TotalHeight() const
            {
                return padding + Prefix(entries.size()) + padding;
            }

        private:
            static float Extent(Entry const& entry)
            {
                return entry.marginTop + entry.height + entry.trailing;
            }

            float Prefix(std::size_t count) const
            {
                auto result = 0.0f;
                for (auto treeIndex = (std::min)(count, entries.size()); treeIndex > 0; treeIndex -= treeIndex & (~treeIndex + 1))
                    result += fenwick[treeIndex];
                return result;
            }

            std::vector<Entry> entries;
            std::vector<float> fenwick;
            float padding = 0.0f;
            bool initialized = false;
        };

        struct MathPreview
        {
            DisplayInlineText display;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            float height = 0.0f;
        };

        struct Block
        {
            DisplayInlineText display;
            ::Microsoft::WRL::ComPtr<IDWriteTextLayout> layout;
            std::vector<EditorInlineImageRenderer::ImageDraw> images;
            std::vector<MathPreview> mathPreviews;
            std::optional<EditorTableBlockRenderer::PreparedTable> table;
            std::vector<elmd::NodeId> owners;
            float textHeight = 0.0f;
            float height = 0.0f;
            elmd::NodeId sourceId{};
            std::uint64_t presentationKey = 0;
            bool sourceMode = false;
            bool code = false;
            bool containsMath = false;
            bool containsImage = false;
            bool embeddedRequested = false;
            bool pendingMath = false;
            bool valid = false;
            std::uint64_t embeddedGeneration = 0;
            std::uint64_t remoteImageGeneration = 0;
        };

        std::uint64_t modelRevision = 0;
        elmd::TextSelection selection{};
        float documentWidth = 0.0f;
        float totalHeight = 0.0f;
        std::uint64_t themeRevision = 0;
        std::vector<Block> blocks;
        GeometryIndex geometry;
        std::unordered_map<std::uint64_t, std::size_t> topLevelBlockIndex;
        std::unordered_map<std::uint64_t, std::size_t> ownerBlockIndex;
        std::unordered_set<std::size_t> embeddedBlocks;
        std::unordered_set<std::size_t> layoutBlocks;
        float lastViewportOffset = 0.0f;
        bool hasLastViewportOffset = false;
    };

}
