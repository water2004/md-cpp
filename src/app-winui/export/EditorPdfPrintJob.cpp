#include "pch.h"
#include "export/EditorPdfPrintJob.h"
#include "localization/Localization.h"

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

    std::unique_ptr<EditorPdfPrintJob> EditorPdfPrintJob::Begin(
        std::filesystem::path const& path,
        std::wstring const& title,
        ID2D1Device* device,
        IWICImagingFactory* wicFactory)
    {
        auto job = std::unique_ptr<EditorPdfPrintJob>(new EditorPdfPrintJob());
        job->Initialize(path, title, device, wicFactory);
        return job;
    }

    EditorPdfPrintJob::~EditorPdfPrintJob()
    {
        ReleaseHandles();
        if (!completed_ && !path_.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }

    void EditorPdfPrintJob::Initialize(
        std::filesystem::path const& path,
        std::wstring const& title,
        ID2D1Device* device,
        IWICImagingFactory* wicFactory)
    {
        if (path.empty() || !device || !wicFactory) winrt::throw_hresult(E_INVALIDARG);
        path_ = path;
        EnsurePdfPrinter();

        winrt::check_hresult(SHCreateStreamOnFileEx(
            path.c_str(),
            STGM_CREATE | STGM_WRITE | STGM_SHARE_EXCLUSIVE,
            FILE_ATTRIBUTE_NORMAL,
            TRUE,
            nullptr,
            output_.GetAddressOf()));

        LARGE_INTEGER start{};
        winrt::check_hresult(output_->Seek(start, STREAM_SEEK_SET, nullptr));

        ::Microsoft::WRL::ComPtr<IPrintDocumentPackageTargetFactory> factory;
        winrt::check_hresult(CoCreateInstance(
            __uuidof(PrintDocumentPackageTargetFactory),
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(factory.GetAddressOf())));

        auto jobName = title.empty()
            ? std::wstring(Localize(L"PdfExportJobName").c_str())
            : title;
        winrt::check_hresult(factory->CreateDocumentPackageTargetForPrintJob(
            PdfPrinterName,
            jobName.c_str(),
            output_.Get(),
            nullptr,
            target_.GetAddressOf()));

        D2D1_PRINT_CONTROL_PROPERTIES properties{};
        properties.rasterDPI = 300.0f;
        properties.fontSubset = D2D1_PRINT_FONT_SUBSET_MODE_DEFAULT;
        properties.colorSpace = D2D1_COLOR_SPACE_SRGB;
        winrt::check_hresult(device->CreatePrintControl(
            wicFactory,
            target_.Get(),
            properties,
            printControl_.GetAddressOf()));
    }

    void EditorPdfPrintJob::AddPage(EditorPdfPage const& page)
    {
        if (completed_ || !printControl_ || !page.commands
            || page.size.width <= 0.0f || page.size.height <= 0.0f)
            winrt::throw_hresult(E_INVALIDARG);
        winrt::check_hresult(printControl_->AddPage(page.commands.Get(), page.size, nullptr));
    }

    void EditorPdfPrintJob::Complete()
    {
        if (completed_ || !printControl_ || !output_) winrt::throw_hresult(E_UNEXPECTED);
        winrt::check_hresult(printControl_->Close());
        winrt::check_hresult(output_->Commit(STGC_DEFAULT));
        completed_ = true;
        ReleaseHandles();
    }

    void EditorPdfPrintJob::ReleaseHandles() noexcept
    {
        printControl_.Reset();
        target_.Reset();
        output_.Reset();
    }
}
