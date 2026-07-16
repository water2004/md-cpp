#pragma once

#include "editor/rendering/EditorSurfaceRenderer.h"
#include "editor/rendering/EditorBlockGeometryIndex.h"
#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorTableBlockRenderer.h"

namespace winrt::ElMd
{
    struct EditorSurfaceRenderer::PreparedDocument
    {
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
        EditorBlockGeometryIndex geometry;
        std::unordered_map<std::uint64_t, std::size_t> ownerBlockIndex;
        std::unordered_set<std::size_t> embeddedBlocks;
        std::unordered_set<std::size_t> layoutBlocks;
        float lastViewportOffset = 0.0f;
        bool hasLastViewportOffset = false;
    };

}
