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
        static void Write(
            std::filesystem::path const& path,
            std::wstring const& title,
            ID2D1Device* device,
            IWICImagingFactory* wicFactory,
            std::span<EditorPdfPage const> pages);
    };
}
