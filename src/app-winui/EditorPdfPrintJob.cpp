#include "pch.h"
#include "EditorPdfPrintJob.h"

namespace winrt::ElMd
{
    namespace
    {
        constexpr wchar_t PdfPrinterName[] = L"Microsoft Print to PDF";

        void EnsurePdfPrinter()
        {
            HANDLE printer = nullptr;
            if (!OpenPrinterW(const_cast<wchar_t*>(PdfPrinterName), &printer, nullptr))
                winrt::throw_hresult(HRESULT_FROM_WIN32(GetLastError()));
            ClosePrinter(printer);
        }
    }

    void EditorPdfPrintJob::Write(
        std::filesystem::path const& path,
        std::wstring const& title,
        ID2D1Device* device,
        IWICImagingFactory* wicFactory,
        std::span<EditorPdfPage const> pages)
    try
    {
        if (path.empty() || !device || !wicFactory || pages.empty()) winrt::throw_hresult(E_INVALIDARG);
        EnsurePdfPrinter();

        ::Microsoft::WRL::ComPtr<IStream> output;
        winrt::check_hresult(SHCreateStreamOnFileEx(
            path.c_str(),
            STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
            FILE_ATTRIBUTE_NORMAL,
            TRUE,
            nullptr,
            output.GetAddressOf()));

        LARGE_INTEGER start{};
        winrt::check_hresult(output->Seek(start, STREAM_SEEK_SET, nullptr));

        ::Microsoft::WRL::ComPtr<IPrintDocumentPackageTargetFactory> factory;
        winrt::check_hresult(CoCreateInstance(
            __uuidof(PrintDocumentPackageTargetFactory),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf())));

        ::Microsoft::WRL::ComPtr<IPrintDocumentPackageTarget> target;
        auto jobName = title.empty() ? std::wstring(L"el-md PDF export") : title;
        winrt::check_hresult(factory->CreateDocumentPackageTargetForPrintJob(
            PdfPrinterName,
            jobName.c_str(),
            output.Get(),
            nullptr,
            target.GetAddressOf()));

        D2D1_PRINT_CONTROL_PROPERTIES properties{};
        properties.rasterDPI = 300.0f;
        properties.fontSubset = D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT;
        properties.colorSpace = D2D1_COLOR_SPACE_SRGB;
        ::Microsoft::WRL::ComPtr<ID2D1PrintControl> printControl;
        winrt::check_hresult(device->CreatePrintControl(
            wicFactory,
            target.Get(),
            properties,
            printControl.GetAddressOf()));

        for (auto const& page : pages)
        {
            if (!page.commands || page.size.width <= 0.0f || page.size.height <= 0.0f)
                winrt::throw_hresult(E_INVALIDARG);
            winrt::check_hresult(printControl->AddPage(page.commands.Get(), page.size, nullptr));
        }
        winrt::check_hresult(printControl->Close());
        winrt::check_hresult(output->Commit(STGC_DEFAULT));
    }
    catch (...)
    {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
        throw;
    }
}
