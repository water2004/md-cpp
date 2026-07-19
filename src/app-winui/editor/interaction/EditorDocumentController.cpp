#include "pch.h"
#include "editor/interaction/EditorDocumentController.h"
#include "editor/interaction/EditorDocumentControllerState.h"
#include "localization/Localization.h"

import folia.core.command;
import folia.core.slug;
import folia.core.utf;

namespace winrt::Folia
{
    EditorDocumentController::EditorDocumentController() : state_(std::make_shared<State>())
    {
    }

    EditorDocumentController::~EditorDocumentController()
    {
        Detach();
    }

    void EditorDocumentController::Attach(
        EditorSession& session,
        EditorSurfaceRenderer& renderer,
        EditorTextInputController& textInput,
        ExecuteCommand executeCommand,
        SetStatus setStatus,
        SetProgress setProgress,
        DocumentChanged documentChanged,
        Render render,
        WindowHandle windowHandle)
    {
        Detach();
        state_->session = &session;
        state_->renderer = &renderer;
        state_->textInput = &textInput;
        state_->executeCommand = std::move(executeCommand);
        state_->setStatus = std::move(setStatus);
        state_->setProgress = std::move(setProgress);
        state_->documentChanged = std::move(documentChanged);
        state_->render = std::move(render);
        state_->windowHandle = std::move(windowHandle);
    }

    void EditorDocumentController::Detach()
    {
        state_->cancelRequested = true;
        state_->pdfExporting = false;
        if (state_->pdfWork) state_->pdfWork->stop.request_stop();
        if (!state_->pdfWork && !state_->pdfOutputPath.empty())
        {
            std::error_code ignored;
            std::filesystem::remove(state_->pdfOutputPath, ignored);
            state_->pdfOutputPath.clear();
        }
        state_->generation.fetch_add(1);
        state_->session = nullptr;
        state_->renderer = nullptr;
        state_->textInput = nullptr;
        state_->executeCommand = {};
        state_->setStatus = {};
        state_->setProgress = {};
        state_->documentChanged = {};
        state_->render = {};
        state_->windowHandle = {};
    }

    void EditorDocumentController::OpenDocument()
    {
        OpenDocumentAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::OpenDocumentPath(winrt::hstring const& path)
    {
        if (path.empty()) return;
        OpenDocumentPathAsync(state_, state_->generation.load(), path);
    }

    void EditorDocumentController::SaveDocument()
    {
        SaveDocumentAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::SaveDocumentAs()
    {
        SaveDocumentAsAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::InsertImage()
    {
        InsertImageAsync(state_, state_->generation.load());
    }

    void EditorDocumentController::OpenLink(std::string href)
    {
        while (!href.empty() && static_cast<unsigned char>(href.front()) <= 0x20) href.erase(href.begin());
        while (!href.empty() && static_cast<unsigned char>(href.back()) <= 0x20) href.pop_back();
        if (href.empty() || !state_->session) return;
        if (href.front() == '#')
        {
            if (auto item = state_->session->RenderModel().outline.find_item_by_url_fragment(href))
            {
                state_->session->SetSelection(item->position, item->position);
                if (state_->textInput) state_->textInput->NotifySelectionChanged();
                if (state_->renderer) state_->renderer->ScrollToPosition(item->position);
                if (state_->render) state_->render();
            }
            return;
        }
        OpenLinkAsync(state_, state_->generation.load(), std::move(href));
    }

    void EditorDocumentController::CopySelection()
    {
        if (!state_->session || !state_->session->HasSelection()) return;
        auto package = winrt::Windows::ApplicationModel::DataTransfer::DataPackage();
        package.SetText(winrt::to_hstring(state_->session->SelectedTextUtf8()));
        winrt::Windows::ApplicationModel::DataTransfer::Clipboard::SetContent(package);
        if (state_->setStatus) state_->setStatus(Localize(L"StatusCopiedSelection"));
    }

    void EditorDocumentController::CutSelection()
    {
        if (!state_->session || !state_->session->HasSelection()) return;
        CopySelection();
        folia::Command command;
        command.kind = folia::CommandKind::DeleteSelection;
        if (state_->executeCommand) state_->executeCommand(command);
    }

    void EditorDocumentController::PasteClipboard()
    {
        PasteClipboardAsync(state_, state_->generation.load());
    }

    bool EditorDocumentController::Active(std::shared_ptr<State> const& state, std::uint64_t generation)
    {
        return state && state->generation.load() == generation && state->session;
    }

    winrt::fire_and_forget EditorDocumentController::OpenDocumentAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            picker.FileTypeFilter().Append(L".md");
            picker.FileTypeFilter().Append(L".markdown");
            picker.FileTypeFilter().Append(L".txt");
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSingleFileAsync();
            if (!Active(state, generation)) co_return;
            if (!file)
            {
                if (state->setStatus) state->setStatus(Localize(L"StatusOpenCancelled"));
                co_return;
            }
            co_await LoadDocumentAsync(state, generation, file);
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation))
            {
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusOpenFailed", { error.message() }));
            }
        }
        catch (std::exception const& error)
        {
            if (Active(state, generation))
            {
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(LocalizeFormat(
                    L"StatusOpenFailed", { winrt::to_hstring(error.what()) }));
            }
        }
    }

    winrt::fire_and_forget EditorDocumentController::OpenDocumentPathAsync(
        std::shared_ptr<State> state,
        std::uint64_t generation,
        winrt::hstring path)
    {
        try
        {
            if (!Active(state, generation)) co_return;
            auto normalized = std::filesystem::path(path.c_str()).lexically_normal();
            if (normalized.is_relative()) normalized = std::filesystem::absolute(normalized);
            auto file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(normalized.wstring());
            if (!Active(state, generation)) co_return;
            co_await LoadDocumentAsync(state, generation, file);
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation))
            {
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusOpenFailed", { error.message() }));
            }
        }
        catch (std::exception const& error)
        {
            if (Active(state, generation))
            {
                if (state->setProgress) state->setProgress(false, std::nullopt, false);
                if (state->setStatus) state->setStatus(LocalizeFormat(
                    L"StatusOpenFailed", { winrt::to_hstring(error.what()) }));
            }
        }
    }

    winrt::Windows::Foundation::IAsyncAction EditorDocumentController::LoadDocumentAsync(
        std::shared_ptr<State> state,
        std::uint64_t generation,
        winrt::Windows::Storage::StorageFile file)
    {
        if (!Active(state, generation) || !file) co_return;
        if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusReadingFile", { file.Name() }));
        if (state->setProgress) state->setProgress(true, std::nullopt, false);
        auto text = co_await winrt::Windows::Storage::FileIO::ReadTextAsync(file);
        if (!Active(state, generation)) co_return;
        if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusParsingFile", { file.Name() }));
        if (state->setProgress) state->setProgress(true, 0.0, false);
        auto dispatcher = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        auto reportedPercent = std::make_shared<std::atomic_size_t>(0);
        auto progressActive = std::make_shared<std::atomic_bool>(true);
        winrt::apartment_context uiContext;
        std::exception_ptr loadFailure;
        std::optional<EditorSession> loaded;
        co_await winrt::resume_background();
        try
        {
            loaded.emplace();
            loaded->Open(file, text, [
                state,
                generation,
                dispatcher,
                reportedPercent,
                progressActive](
                std::size_t consumed,
                std::size_t total)
            {
                if (!progressActive->load()) return;
                auto percent = total == 0
                    ? std::size_t{100}
                    : (std::min)(std::size_t{100}, consumed * 100 / total);
                auto previous = reportedPercent->load();
                while (percent > previous
                    && !reportedPercent->compare_exchange_weak(previous, percent)) {}
                if (percent <= previous) return;
                dispatcher.TryEnqueue([state, generation, percent, progressActive]
                {
                    if (!progressActive->load()
                        || !Active(state, generation)
                        || !state->setProgress) return;
                    state->setProgress(true, static_cast<double>(percent) / 100.0, false);
                });
            });
        }
        catch (...)
        {
            loadFailure = std::current_exception();
        }
        progressActive->store(false);
        co_await uiContext;
        if (!Active(state, generation)) co_return;
        if (loadFailure) std::rethrow_exception(loadFailure);
        *state->session = std::move(*loaded);
        if (state->renderer)
        {
            state->renderer->ResetDocumentCaches();
            state->renderer->SetScrollOffset(0.0f);
        }
        if (state->textInput) state->textInput->NotifyTextChanged();
        if (state->documentChanged) state->documentChanged();
        if (state->setProgress) state->setProgress(false, std::nullopt, false);
        if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusOpenedFile", { file.Name() }));
    }

    winrt::fire_and_forget EditorDocumentController::SaveDocumentAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation)) co_return;
            if (!state->session->HasFile())
            {
                SaveDocumentAsAsync(state, generation);
                co_return;
            }
            auto file = state->session->File();
            auto text = state->session->Text();
            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, text);
            if (!Active(state, generation)) co_return;
            if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusSavedFile", { file.Name() }));
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus)
                state->setStatus(LocalizeFormat(L"StatusSaveFailed", { error.message() }));
        }
    }

    winrt::fire_and_forget EditorDocumentController::SaveDocumentAsAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileSavePicker();
            picker.DefaultFileExtension(L".md");
            picker.SuggestedFileName(Localize(L"UntitledMarkdown"));
            picker.FileTypeChoices().Insert(Localize(L"MarkdownFileType"), winrt::single_threaded_vector<winrt::hstring>({ L".md" }));
            picker.FileTypeChoices().Insert(Localize(L"TextFileType"), winrt::single_threaded_vector<winrt::hstring>({ L".txt" }));
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSaveFileAsync();
            if (!Active(state, generation)) co_return;
            if (!file)
            {
                if (state->setStatus) state->setStatus(Localize(L"StatusSaveCancelled"));
                co_return;
            }
            co_await winrt::Windows::Storage::FileIO::WriteTextAsync(file, state->session->Text());
            if (!Active(state, generation)) co_return;
            state->session->SaveAs(file);
            if (state->documentChanged) state->documentChanged();
            if (state->setStatus) state->setStatus(LocalizeFormat(L"StatusSavedFile", { file.Name() }));
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus)
                state->setStatus(LocalizeFormat(L"StatusSaveFailed", { error.message() }));
        }
    }

    winrt::fire_and_forget EditorDocumentController::InsertImageAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation) || !state->windowHandle) co_return;
            auto picker = winrt::Windows::Storage::Pickers::FileOpenPicker();
            for (auto extension : {
                L".svg",
                L".png",
                L".jpg",
                L".jpeg",
                L".gif",
                L".bmp",
                L".tif",
                L".tiff",
                L".ico",
                L".webp",
                L".heic",
                L".heif",
                L".avif",
                L".jxr",
                L".wdp",
            }) picker.FileTypeFilter().Append(extension);
            auto initializeWithWindow = picker.as<IInitializeWithWindow>();
            winrt::check_hresult(initializeWithWindow->Initialize(state->windowHandle()));
            auto file = co_await picker.PickSingleFileAsync();
            if (!Active(state, generation) || !file) co_return;
            std::filesystem::path path(file.Path().c_str());
            if (state->session->HasFile())
            {
                std::error_code error;
                auto base = std::filesystem::path(state->session->Path().c_str()).parent_path();
                auto relative = std::filesystem::relative(path, base, error);
                if (!error && !relative.empty()) path = std::move(relative);
            }
            folia::Command command;
            command.kind = folia::CommandKind::InsertImage;
            command.path = folia::utf8_to_cps(winrt::to_string(winrt::hstring(path.generic_wstring())));
            if (state->executeCommand) state->executeCommand(command);
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus)
                state->setStatus(LocalizeFormat(L"StatusImageInsertFailed", { error.message() }));
        }
    }

    winrt::fire_and_forget EditorDocumentController::OpenLinkAsync(std::shared_ptr<State> state, std::uint64_t generation, std::string href)
    {
        auto lower = href;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
        try
        {
            if (!Active(state, generation)) co_return;
            if (lower.starts_with("https://") || lower.starts_with("http://") || lower.starts_with("mailto:"))
            {
                co_await winrt::Windows::System::Launcher::LaunchUriAsync(winrt::Windows::Foundation::Uri(winrt::to_hstring(href)));
                co_return;
            }
            auto colon = lower.find(':');
            auto boundary = lower.find_first_of("/?#");
            if (colon != std::string::npos && (boundary == std::string::npos || colon < boundary)) co_return;
            const auto suffix = href.find_first_of("?#");
            const auto encodedPath = suffix == std::string::npos
                ? std::string_view{href}
                : std::string_view{href}.substr(0, suffix);
            const auto decodedPath = folia::percent_decode_url_component(encodedPath);
            if (decodedPath.empty()) co_return;
            auto path = std::filesystem::path(winrt::to_hstring(decodedPath).c_str());
            if (path.is_relative()) path = std::filesystem::path(state->session->BaseDirectory()) / path;
            auto file = co_await winrt::Windows::Storage::StorageFile::GetFileFromPathAsync(path.lexically_normal().wstring());
            if (!Active(state, generation)) co_return;
            co_await winrt::Windows::System::Launcher::LaunchFileAsync(file);
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus)
                state->setStatus(LocalizeFormat(L"StatusOpenLinkFailed", { error.message() }));
        }
    }

    winrt::fire_and_forget EditorDocumentController::PasteClipboardAsync(std::shared_ptr<State> state, std::uint64_t generation)
    {
        try
        {
            if (!Active(state, generation)) co_return;
            auto content = winrt::Windows::ApplicationModel::DataTransfer::Clipboard::GetContent();
            if (!content.Contains(winrt::Windows::ApplicationModel::DataTransfer::StandardDataFormats::Text())) co_return;
            auto text = co_await content.GetTextAsync();
            if (!Active(state, generation) || text.empty()) co_return;
            if (state->executeCommand)
            {
                folia::Command command;
                command.kind = folia::CommandKind::Paste;
                command.text = folia::utf8_to_cps(winrt::to_string(text));
                state->executeCommand(command);
            }
        }
        catch (winrt::hresult_error const& error)
        {
            if (Active(state, generation) && state->setStatus)
                state->setStatus(LocalizeFormat(L"StatusPasteFailed", { error.message() }));
        }
    }
}
