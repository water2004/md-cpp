#include "pch.h"
#include "editor/interaction/EditorDocumentController.h"
#include "editor/interaction/EditorDocumentControllerState.h"
#include "localization/Localization.h"

namespace winrt::Folia
{
    void EditorDocumentController::ExportPdf()
    {
        ExportPdfAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::CancelOperation()
    {
        if (!state_->pdfExporting) return;
        state_->cancelRequested = true;
        if (state_->pdfWork)
        {
            state_->pdfWork->stop.request_stop();
            auto total = state_->pdfWork->totalPages.load();
            auto value = total == 0
                ? std::optional<double>{}
                : std::optional<double>{
                    static_cast<double>(state_->pdfWork->completedPages.load()) / total};
            if (state_->setProgress) state_->setProgress(true, value, false);
            if (state_->setStatus) state_->setStatus(Localize(L"StatusCancellingPdf"));
            return;
        }
        state_->pdfExporting = false;
        if (!state_->pdfOutputPath.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(state_->pdfOutputPath, ignored);
            state_->pdfOutputPath.clear();
        }
        if (state_->setProgress) state_->setProgress(false, std::nullopt, false);
        if (state_->setStatus) state_->setStatus(Localize(L"StatusPdfCancelled"));
    }

    winrt::fire_and_forget EditorDocumentController::ExportPdfAsync(
        std::shared_ptr<State> state,
        std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle || !state->renderer) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileSavePicker();
            picker.DefaultFileExtension(L".pdf");
            auto displayName = state->session->DisplayName();
            auto suggested = std::filesystem::path(displayName.c_str()).stem().wstring();
            picker.SuggestedFileName(suggested.empty()
                ? Localize(L"UntitledPdf")
                : winrt::hstring(suggested + L".pdf"));
            picker.FileTypeChoices().Insert(
                Localize(L"PdfFileType"),
                winrt::single_threaded_vector<winrt::hstring>({L".pdf"}));
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSaveFileAsync();
            if (!Active(state, generation)) co_return;
            if (!file)
            {
                if (state->setStatus) state->setStatus(Localize(L"StatusPdfCancelled"));
                co_return;
            }

            state->cancelRequested = false;
            state->pdfExporting = true;
            state->pdfOutputPath = std::filesystem::path(file.Path().c_str());
            if (state->setStatus) state->setStatus(Localize(L"StatusPreparingPdf"));
            if (state->setProgress) state->setProgress(true, std::nullopt, true);
            winrt::apartment_context uiContext;
            co_await winrt::resume_after(std::chrono::milliseconds(16));
            co_await uiContext;
            if (!Active(state, generation)) co_return;
            if (state->cancelRequested)
            {
                state->pdfExporting = false;
                if (!state->pdfOutputPath.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(state->pdfOutputPath, ignored);
                    state->pdfOutputPath.clear();
                }
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(Localize(L"StatusPdfCancelled"));
                co_return;
            }

            // Freeze the render model that existed when the picker closed.
            // The independent worker owns this snapshot and all of its D2D,
            // DirectWrite, WIC, MathJax, and print resources.
            auto renderModel = state->session->BuildPrintRenderModel();
            auto baseDirectory = state->session->BaseDirectory();
            auto title = std::filesystem::path(displayName.c_str()).stem().wstring();
            auto outputPath = state->pdfOutputPath;
            auto theme = state->renderer->Theme();
            auto mathEnabled = state->renderer->MathRenderingEnabled();
            if (state->cancelRequested)
            {
                state->pdfExporting = false;
                std::error_code ignored;
                std::filesystem::remove(outputPath, ignored);
                state->pdfOutputPath.clear();
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(Localize(L"StatusPdfCancelled"));
                co_return;
            }

            auto work = std::make_shared<detail::PdfExportWork>();
            state->pdfWork = work;
            std::jthread worker([
                work,
                outputPath,
                title = std::move(title),
                baseDirectory = std::move(baseDirectory),
                renderModel = std::move(renderModel),
                theme = std::move(theme),
                mathEnabled](std::stop_token threadStop) mutable
            {
                struct ApartmentScope
                {
                    ApartmentScope()
                    {
                        winrt::init_apartment(winrt::apartment_type::multi_threaded);
                        initialized = true;
                    }
                    ~ApartmentScope()
                    {
                        if (initialized) winrt::uninit_apartment();
                    }
                    bool initialized = false;
                };

                try
                {
                    ApartmentScope apartment;
                    {
                        EditorSurfaceRenderer renderer;
                        renderer.SetTheme(theme);
                        renderer.SetMathRenderingEnabled(mathEnabled);
                        renderer.InitializeForPdf();
                        detail::EditorRenderFrame frame{
                            renderModel,
                            {},
                            {},
                            baseDirectory,
                            {},
                            {},
                        };
                        for (;;)
                        {
                            if (threadStop.stop_requested() || work->stop.stop_requested())
                            {
                                renderer.SetMathRenderingEnabled(false);
                                renderer.CancelPdfExport();
                                work->cancelled = true;
                                std::error_code ignored;
                                std::filesystem::remove(outputPath, ignored);
                                break;
                            }
                            auto progress = renderer.ExportPdfStep(outputPath, title, frame);
                            work->completedPages = progress.completedPages;
                            work->totalPages = progress.totalPages;
                            if (progress.result == EditorPdfExportResult::Completed) break;
                            auto delay = progress.result == EditorPdfExportResult::WaitingForAssets
                                ? std::chrono::milliseconds(40)
                                : std::chrono::milliseconds(1);
                            auto deadline = std::chrono::steady_clock::now() + delay;
                            while (std::chrono::steady_clock::now() < deadline
                                && !threadStop.stop_requested()
                                && !work->stop.stop_requested())
                                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        }
                    }
                }
                catch (...)
                {
                    work->failure = std::current_exception();
                    std::error_code ignored;
                    std::filesystem::remove(outputPath, ignored);
                }
                work->done.store(true, std::memory_order_release);
            });

            while (!work->done.load(std::memory_order_acquire))
            {
                if (!Active(state, generation)) work->stop.request_stop();
                if (Active(state, generation))
                {
                    auto stopping = work->stop.stop_requested();
                    auto completed = work->completedPages.load();
                    auto total = work->totalPages.load();
                    auto value = total == 0
                        ? std::optional<double>{}
                        : std::optional<double>{static_cast<double>(completed) / total};
                    if (state->setProgress) state->setProgress(true, value, !stopping);
                    if (state->setStatus)
                    {
                        if (stopping)
                            state->setStatus(Localize(L"StatusCancellingPdf"));
                        else if (total > 0)
                            state->setStatus(LocalizeFormat(
                                L"StatusExportingPdfPage",
                                { winrt::to_hstring(completed), winrt::to_hstring(total) }));
                        else
                            state->setStatus(Localize(L"StatusPreparingPdfAssets"));
                    }
                }
                co_await winrt::resume_after(std::chrono::milliseconds(16));
                co_await uiContext;
            }
            if (worker.joinable()) worker.join();
            if (state->pdfWork == work) state->pdfWork.reset();
            if (!Active(state, generation)) co_return;

            state->pdfExporting = false;
            state->pdfOutputPath.clear();
            if (state->setProgress) state->setProgress(false, std::nullopt, false);
            if (work->failure) std::rethrow_exception(work->failure);
            if (work->cancelled)
            {
                if (state->setStatus) state->setStatus(Localize(L"StatusPdfCancelled"));
                co_return;
            }
            if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusExportedPdf", { file.Path() }));
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation))
            {
                if (state->pdfWork) state->pdfWork->stop.request_stop();
                state->pdfWork.reset();
                state->pdfExporting = false;
                if (!state->pdfOutputPath.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(state->pdfOutputPath, ignored);
                }
                state->pdfOutputPath.clear();
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusPdfFailed", { error.message() }));
            }
        }
        catch (std::exception const& error)
        {
            if (Active(state, generation))
            {
                if (state->pdfWork) state->pdfWork->stop.request_stop();
                state->pdfWork.reset();
                state->pdfExporting = false;
                if (!state->pdfOutputPath.empty())
                {
                    std::error_code ignored;
                    std::filesystem::remove(state->pdfOutputPath, ignored);
                }
                state->pdfOutputPath.clear();
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(LocalizeFormat(
                    L"StatusPdfFailed", { winrt::to_hstring(error.what()) }));
            }
        }
    }
}
