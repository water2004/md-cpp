#pragma once

import folia.platform.editor_geometry;

#include "editor/rendering/EditorContentPreparation.h"
#include "editor/rendering/EditorInlineImageRenderer.h"
#include "editor/rendering/EditorTableBlockRenderer.h"

namespace winrt::Folia
{
    using folia::platform::editor::EditorBlockGeometryIndex;

    struct EditorPreparedDocument
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
            std::vector<AsyncWorkDependency> pendingMathJaxDependencies;
            std::vector<AsyncWorkDependencyGroup> pendingSvgDependencyGroups;
            std::optional<EditorTableBlockRenderer::PreparedTable> table;
            std::vector<folia::NodeId> owners;
            float textHeight = 0.0f;
            float height = 0.0f;
            folia::NodeId sourceId{};
            std::uint64_t presentationKey = 0;
            bool sourceMode = false;
            bool code = false;
            bool containsMath = false;
            bool containsImage = false;
            bool embeddedRequested = false;
            bool pendingMath = false;
            bool pendingImage = false;
            bool valid = false;
            std::uint64_t embeddedGeneration = 0;
            std::uint64_t remoteImageGeneration = 0;

            // Drop heavyweight DirectWrite/D2D/image state while retaining
            // exact measured geometry and stable block metadata. PDF export
            // uses this after each page so its working set is page-local.
            void ReleaseVisualContent()
            {
                auto measuredHeight = height;
                auto retainedSourceId = sourceId;
                auto retainedPresentationKey = presentationKey;
                auto retainedOwners = std::move(owners);
                auto retainedSourceMode = sourceMode;
                auto retainedCode = code;
                auto retainedContainsMath = containsMath;
                auto retainedContainsImage = containsImage;
                auto retainedPendingImage = pendingImage;
                *this = {};
                height = measuredHeight;
                sourceId = retainedSourceId;
                presentationKey = retainedPresentationKey;
                owners = std::move(retainedOwners);
                sourceMode = retainedSourceMode;
                code = retainedCode;
                containsMath = retainedContainsMath;
                containsImage = retainedContainsImage;
                pendingImage = retainedPendingImage;
            }
        };

        std::uint64_t modelRevision = 0;
        folia::TextSelection selection{};
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
