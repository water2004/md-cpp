// folia.platform.editor_selection_drag_model — deterministic drag projection.
export module folia.platform.editor_selection_drag_model;
import std;

export namespace folia::platform::editor
{
    struct EditorSelectionDragPolicy
    {
        float minimumAutoScrollSpeed = 80.0f;
        float maximumAutoScrollSpeed = 2400.0f;
        float linearSpeedPerDip = 8.0f;
        float quadraticSpeedPerDip = 0.02f;
        float bottomHitInset = 0.5f;
    };

    struct EditorSelectionPointerProjection
    {
        float hitX = 0.0f;
        float hitY = 0.0f;
        float autoScrollVelocity = 0.0f;
    };

    inline EditorSelectionPointerProjection ProjectEditorSelectionPointer(
        float pointerX,
        float pointerY,
        float viewportWidth,
        float viewportHeight,
        EditorSelectionDragPolicy policy = {})
    {
        pointerX = std::isfinite(pointerX) ? pointerX : 0.0f;
        pointerY = std::isfinite(pointerY) ? pointerY : 0.0f;
        viewportWidth = std::isfinite(viewportWidth)
            ? (std::max)(0.0f, viewportWidth)
            : 0.0f;
        viewportHeight = std::isfinite(viewportHeight)
            ? (std::max)(0.0f, viewportHeight)
            : 0.0f;

        auto velocity = 0.0f;
        if (viewportHeight > 0.0f
            && (pointerY < 0.0f || pointerY > viewportHeight))
        {
            auto distance = pointerY < 0.0f
                ? -pointerY
                : pointerY - viewportHeight;
            auto speed = policy.minimumAutoScrollSpeed
                + policy.linearSpeedPerDip * distance
                + policy.quadraticSpeedPerDip * distance * distance;
            speed = (std::clamp)(
                speed,
                0.0f,
                (std::max)(0.0f, policy.maximumAutoScrollSpeed));
            velocity = pointerY < 0.0f ? -speed : speed;
        }

        auto hitX = viewportWidth > 0.0f
            ? (std::clamp)(pointerX, 0.0f, viewportWidth)
            : pointerX;
        auto hitBottom = (std::max)(
            0.0f,
            viewportHeight - (std::max)(0.0f, policy.bottomHitInset));
        auto hitY = viewportHeight > 0.0f
            ? (std::clamp)(pointerY, 0.0f, hitBottom)
            : pointerY;
        return {
            .hitX = hitX,
            .hitY = hitY,
            .autoScrollVelocity = velocity,
        };
    }
}
