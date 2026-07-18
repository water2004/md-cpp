// folia.platform.editor_scroll_state — deterministic editor scroll state.
export module folia.platform.editor_scroll_state;
import std;

export namespace folia::platform::editor
{
    class EditorScrollState
    {
    public:
        struct Snapshot
        {
            float offset = 0.0f;
            float target = 0.0f;
        };

        float Offset() const noexcept { return offset_; }
        float Target() const noexcept { return target_; }
        Snapshot Save() const noexcept { return {offset_, target_}; }

        void Set(float value, float maximum)
        {
            offset_ = ClampValue(value, maximum);
            target_ = offset_;
        }

        void Restore(Snapshot value, float maximum)
        {
            offset_ = ClampValue(value.offset, maximum);
            target_ = ClampValue(value.target, maximum);
        }

        void Clamp(float maximum)
        {
            offset_ = ClampValue(offset_, maximum);
            target_ = ClampValue(target_, maximum);
        }

        void Shift(float delta, float maximum)
        {
            if (!std::isfinite(delta) || delta == 0.0f) return;
            offset_ = ClampValue(offset_ + delta, maximum);
            target_ = ClampValue(target_ + delta, maximum);
        }

        void Queue(float delta, float maximum)
        {
            if (!std::isfinite(delta) || delta == 0.0f) return;
            auto pendingDistance = target_ - offset_;
            if (pendingDistance * delta < 0.0f) target_ = offset_;
            target_ = ClampValue(target_ + delta, maximum);
        }

        bool Advance(float elapsedSeconds)
        {
            auto distance = target_ - offset_;
            auto elapsed = (std::max)(0.0f, elapsedSeconds);
            if (std::fabs(distance) < 0.1f)
            {
                offset_ = target_;
                return false;
            }

            // Actual elapsed time and an analytic exponential response give
            // one monotonic deceleration phase independent of frame rate.
            constexpr float responseHalfLifeSeconds = 0.075f;
            auto response = 1.0f - std::exp2(-elapsed / responseHalfLifeSeconds);
            offset_ += distance * response;
            return true;
        }

    private:
        static float ClampValue(float value, float maximum)
        {
            if (!std::isfinite(value)) return 0.0f;
            maximum = std::isfinite(maximum) ? (std::max)(0.0f, maximum) : 0.0f;
            return (std::clamp)(value, 0.0f, maximum);
        }

        float offset_ = 0.0f;
        float target_ = 0.0f;
    };
}
