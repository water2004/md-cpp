#pragma once

namespace winrt::Folia
{
    // Creates device-dependent SVG documents away from the presentation
    // thread. Documents are shareable between contexts from the same
    // multi-threaded D2D device, so painting only needs a cache lookup.
    class EditorSvgDocumentCache final
    {
    public:
        EditorSvgDocumentCache();
        ~EditorSvgDocumentCache();
        EditorSvgDocumentCache(EditorSvgDocumentCache const&) = delete;
        EditorSvgDocumentCache& operator=(EditorSvgDocumentCache const&) = delete;

        void Attach(
            winrt::Microsoft::UI::Dispatching::DispatcherQueue const& dispatcher,
            std::function<void()> invalidate);
        void Detach();
        void Configure(ID2D1DeviceContext5* context);
        void Clear();

        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> Find(std::uint64_t renderId);
        bool Queue(
            std::uint64_t renderId,
            std::string const& source,
            float width,
            float height,
            bool highPriority);
        ::Microsoft::WRL::ComPtr<ID2D1SvgDocument> FindOrCreate(
            ID2D1DeviceContext5* context,
            std::uint64_t renderId,
            std::string const& source,
            float width,
            float height);

    private:
        struct State;
        static void Schedule(std::shared_ptr<State> const& state);
        static void ProcessOne(std::shared_ptr<State> const& state);

        std::shared_ptr<State> state;
    };
}
