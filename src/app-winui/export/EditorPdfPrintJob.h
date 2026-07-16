#pragma once

namespace winrt::ElMd
{
    struct EditorPdfPage
    {
        ::Microsoft::WRL::ComPtr<ID2D1CommandList> commands;
        D2D_SIZE_F size{};
    };

    struct EditorPdfPrintJob
    {
        static std::unique_ptr<EditorPdfPrintJob> Begin(
            std::filesystem::path const& path,
            std::wstring const& title,
            ID2D1Device* device,
            IWICImagingFactory* wicFactory);

        ~EditorPdfPrintJob();
        EditorPdfPrintJob(EditorPdfPrintJob const&) = delete;
        EditorPdfPrintJob& operator=(EditorPdfPrintJob const&) = delete;

        void AddPage(EditorPdfPage const& page);
        void Complete();

    private:
        EditorPdfPrintJob() = default;
        void Initialize(
            std::filesystem::path const& path,
            std::wstring const& title,
            ID2D1Device* device,
            IWICImagingFactory* wicFactory);
        void ReleaseHandles() noexcept;

        std::filesystem::path path_;
        ::Microsoft::WRL::ComPtr<IStream> output_;
        ::Microsoft::WRL::ComPtr<IPrintDocumentPackageTarget> target_;
        ::Microsoft::WRL::ComPtr<ID2D1PrintControl> printControl_;
        bool completed_ = false;
    };
}
